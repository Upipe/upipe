/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe filter processing vertical interval analogue data
 */

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-filters/upipe_filter_vbi.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

#include <bitstream/dvb/vbi.h>
#include <bitstream/dvb/telx.h>

#include <libzvbi.h>

/** 34 telx frames of 46 octets */
#define MAX_TELX_SIZE (1 + 34 * 46)
/** from my dice */
#define MAX_CEA708_SIZE 4096

/** @internal @This is the private context of an output of a vbi pipe */
struct upipe_vbi_output {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_vbi_output_check(struct upipe *upipe,
                                   struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_vbi_output, upipe, UPIPE_VBI_OUTPUT_SIGNATURE)
UPIPE_HELPER_OUTPUT(upipe_vbi_output, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_vbi_output, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_vbi_output_check,
                      upipe_vbi_output_register_output_request,
                      upipe_vbi_output_unregister_output_request)

/** @internal upipe_vbi private structure */
struct upipe_vbi {
    /** refcount management structure */
    struct urefcount urefcount;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during request) */
    struct uchain blockers;

    /** subpipe manager */
    struct upipe_mgr sub_mgr;
    /** ttx subpipe */
    struct upipe_vbi_output ttx_subpipe;
    /** cea708 subpipe */
    struct upipe_vbi_output cea708_subpipe;

    /* input data */
    /** input flow_def */
    struct uref *flow_def;
    /** Frame rate */
    struct urational fps;

    /** zvbi decoder */
    struct vbi_raw_decoder vbi_dec;

    /* TTX data */
    /** uref to be sent at the end of this packet */
    struct uref *telx_uref;
    /** beginning of the buffer */
    uint8_t *telx_begin;
    /** current write position */
    uint8_t *telx_w;
    /** maximum write position */
    uint8_t *telx_end;
    /** currently signalled octet rate */
    uint64_t telx_octetrate;

    /* CEA708 data */
    /** uref to be sent at the end of this packet */
    struct uref *cea708_uref;
    /** beginning of the buffer */
    uint8_t *cea708_begin;
    /** current write position */
    uint8_t *cea708_w;
    /** maximum write position */
    uint8_t *cea708_end;

    /** public structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_vbi_resume(struct upipe *upipe);
/** @hidden */
static bool upipe_vbi_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_vbi, upipe, UPIPE_VBI_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_vbi, urefcount, upipe_vbi_free)
UPIPE_HELPER_INPUT(upipe_vbi, urefs, nb_urefs, max_urefs, blockers, upipe_vbi_handle)

UBASE_FROM_TO(upipe_vbi, upipe_mgr, sub_mgr, sub_mgr)
UBASE_FROM_TO(upipe_vbi, upipe_vbi_output, ttx_subpipe, ttx_subpipe)
UBASE_FROM_TO(upipe_vbi, upipe_vbi_output, cea708_subpipe, cea708_subpipe)

/** @This is a table to reverse bits for teletext.
 * http://graphics.stanford.edu/~seander/bithacks.html#BitReverseTable */
static const uint8_t reverse_tab[256] =
{
#   define R2(n)    n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
    R6(0), R6(2), R6(1), R6(3)
};

#define REVERSE(x) reverse_tab[(x)]

/** @internal @This initializes an output subpipe of a vbi pipe.
 *
 * @param upipe pointer to subpipe
 * @param sub_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_vbi_output_init(struct upipe *upipe,
        struct upipe_mgr *sub_mgr, struct uprobe *uprobe)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_sub_mgr(sub_mgr);
    upipe_init(upipe, sub_mgr, uprobe);
    upipe->refcount = &upipe_vbi->urefcount;

    upipe_vbi_output_init_output(upipe);
    upipe_vbi_output_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_vbi_output_check(struct upipe *upipe,
                                   struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_vbi_output_store_flow_def(upipe, flow_format);

    struct upipe_vbi_output *upipe_vbi_output =
        upipe_vbi_output_from_upipe(upipe);
    if (upipe_vbi_output->ubuf_mgr == NULL)
        return UBASE_ERR_NONE;

    struct upipe_vbi *upipe_vbi = upipe_vbi_from_sub_mgr(upipe->mgr);
    upipe_vbi_resume(upipe_vbi_to_upipe(upipe_vbi));
    return UBASE_ERR_NONE;
}

/** @internal @This builds the flow definition of the output subpipe.
 *
 * @param upipe pointer to subpipe
 */
static void upipe_vbi_output_build_flow_def(struct upipe *upipe)
{
    struct upipe_vbi_output *upipe_vbi_output =
        upipe_vbi_output_from_upipe(upipe);
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_sub_mgr(upipe->mgr);
    struct uref *flow_def = uref_dup(upipe_vbi->flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    if (upipe_vbi_output == upipe_vbi_to_ttx_subpipe(upipe_vbi)) {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, "block.dvb_teletext.pic.sub."))
        if (upipe_vbi->telx_octetrate)
            UBASE_ERROR(upipe, uref_block_flow_set_octetrate(flow_def,
                        upipe_vbi->telx_octetrate))

        upipe_vbi_output_require_ubuf_mgr(upipe, flow_def);

    } else {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, "block.cea708.pic.sub."))
        upipe_vbi_output_require_ubuf_mgr(upipe, flow_def);
    }
}

/** @internal @This processes control commands on a vbi output pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_vbi_output_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_vbi_output_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_vbi_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_vbi_output_set_output(upipe, output);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_vbi_to_upipe(upipe_vbi_from_sub_mgr(upipe->mgr));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}


/** @This clean up an output subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vbi_output_clean(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_vbi_output_clean_ubuf_mgr(upipe);
    upipe_vbi_output_clean_output(upipe);

    upipe_clean(upipe);
}

/** @internal @This initializes the output manager for a vbi pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vbi_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_vbi->sub_mgr;
    sub_mgr->refcount = NULL;
    sub_mgr->signature = UPIPE_VBI_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = NULL;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_vbi_output_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a vbi pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_vbi_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    if (signature != UPIPE_VBI_SIGNATURE)
        return NULL;
    struct uprobe *uprobe_ttx = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_cea708 = va_arg(args, struct uprobe *);

    struct upipe_vbi *upipe_vbi = malloc(sizeof(struct upipe_vbi));
    if (unlikely(upipe_vbi == NULL)) {
        uprobe_release(uprobe_ttx);
        uprobe_release(uprobe_cea708);
        return NULL;
    }

    struct upipe *upipe = upipe_vbi_to_upipe(upipe_vbi);
    upipe_init(upipe, mgr, uprobe);

    upipe_vbi_init_urefcount(upipe);
    upipe_vbi_init_input(upipe);
    upipe_vbi_init_sub_mgr(upipe);
    upipe_vbi->flow_def = NULL;
    upipe_vbi->fps.num = 25;
    upipe_vbi->fps.den = 1;
    upipe_vbi->telx_uref = NULL;
    upipe_vbi->telx_octetrate = 0;
    upipe_vbi->cea708_uref = NULL;

    vbi_raw_decoder_init(&upipe_vbi->vbi_dec);
    upipe_vbi->vbi_dec.scanning = 625; /* FIXME: PAL only for now */
    upipe_vbi->vbi_dec.sampling_format = VBI_PIXFMT_YUV420;
    upipe_vbi->vbi_dec.sampling_rate = 13.5e6;
    upipe_vbi->vbi_dec.bytes_per_line = 720;
#if 0
    upipe_vbi->vbi_dec.start[0] = 7;
    upipe_vbi->vbi_dec.start[1] = 320;
    upipe_vbi->vbi_dec.count[0] = 16;
    upipe_vbi->vbi_dec.count[1] = 16;
#else
    upipe_vbi->vbi_dec.start[0] = 1;
    upipe_vbi->vbi_dec.start[1] = 313;
    upipe_vbi->vbi_dec.count[0] = 22;
    upipe_vbi->vbi_dec.count[1] = 23;
#endif
    upipe_vbi->vbi_dec.interlaced = 0;
    upipe_vbi->vbi_dec.synchronous = 1;
    upipe_vbi->vbi_dec.offset = 128;

    vbi_set_log_fn(/* mask: log everything */ -1,
            vbi_log_on_stderr, /* user_data */ NULL);

    int s = vbi_raw_decoder_add_services(&upipe_vbi->vbi_dec,
            VBI_SLICED_TELETEXT_B /*| VBI_SLICED_TELETEXT_C_625*/,
//            VBI_SLICED_VBI_625,
            2 /* strict */);
    assert(s);

    upipe_vbi_output_init(upipe_vbi_output_to_upipe(
                                upipe_vbi_to_ttx_subpipe(upipe_vbi)),
                              &upipe_vbi->sub_mgr, uprobe_ttx);
    upipe_vbi_output_init(upipe_vbi_output_to_upipe(
                                upipe_vbi_to_cea708_subpipe(upipe_vbi)),
                              &upipe_vbi->sub_mgr, uprobe_cea708);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This resumes the vbi pipe by processing the input queue.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vbi_resume(struct upipe *upipe)
{
    bool was_buffered = !upipe_vbi_check_input(upipe);
    upipe_vbi_output_input(upipe);
    upipe_vbi_unblock_input(upipe);
    if (was_buffered && upipe_vbi_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_vbi_input. */
        upipe_release(upipe);
    }
}

/** @internal @This allocates an uref for telx data.
 *
 * @param upipe description structure of the pipe
 * @param uref incoming uref structure
 */
static void upipe_vbi_alloc_telx(struct upipe *upipe, struct uref *uref)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    struct upipe_vbi_output *upipe_vbi_ttx =
        upipe_vbi_to_ttx_subpipe(upipe_vbi);
    struct ubuf *ubuf = ubuf_block_alloc(upipe_vbi_ttx->ubuf_mgr,
                                         MAX_TELX_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_vbi->telx_uref = uref_fork(uref, ubuf);
    if (unlikely(upipe_vbi->telx_uref == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    int size = -1;
    uint8_t *w;
    if (unlikely(!ubase_check(uref_block_write(upipe_vbi->telx_uref, 0,
                                               &size, &w)))) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_vbi->telx_uref);
        upipe_vbi->telx_uref = NULL;
        return;
    }

    w[0] = DVBVBI_DATA_IDENTIFIER;
    upipe_vbi->telx_begin = w;
    upipe_vbi->telx_w = w + 1;
    upipe_vbi->telx_end = w + size;
}

/** @internal @This completes an uref for telx data, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_vbi_output_telx(struct upipe *upipe,
                                   struct upump **upump_p)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    struct upipe_vbi_output *upipe_vbi_ttx =
        upipe_vbi_to_ttx_subpipe(upipe_vbi);

    unsigned int nb_units =
        (upipe_vbi->telx_w - (upipe_vbi->telx_begin + 1)) /
        (DVBVBI_LENGTH + DVBVBI_UNIT_HEADER_SIZE);
    unsigned int nb_units_rounded = 3 + (nb_units / 4) * 4;

    while (nb_units < nb_units_rounded) {
        upipe_vbi->telx_w[0] = DVBVBI_ID_STUFFING;
        upipe_vbi->telx_w[1] = DVBVBI_LENGTH;
        memset(&upipe_vbi->telx_w[2], 0xff, 43);

        upipe_vbi->telx_w += DVBVBI_LENGTH + DVBVBI_UNIT_HEADER_SIZE;
        nb_units++;
    }

    uref_block_unmap(upipe_vbi->telx_uref, 0);
    uref_block_resize(upipe_vbi->telx_uref, 0,
                      upipe_vbi->telx_w - upipe_vbi->telx_begin);

    /* Adjust flow def */
    uint64_t octetrate =
        ((upipe_vbi->telx_w - upipe_vbi->telx_begin) * upipe_vbi->fps.num +
         upipe_vbi->fps.den - 1) / upipe_vbi->fps.den;
    if (upipe_vbi->telx_octetrate != octetrate) {
        upipe_vbi->telx_octetrate = octetrate;
        upipe_vbi_output_build_flow_def(
                upipe_vbi_output_to_upipe(upipe_vbi_ttx));
    }

    upipe_vbi_output_output(upipe_vbi_output_to_upipe(upipe_vbi_ttx),
                             upipe_vbi->telx_uref, upump_p);
    upipe_vbi->telx_uref = NULL;
}

/** @internal @This processes a TTX VBI packet
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param r pointer to the structure
 * @param size size of the structure
 */
static void upipe_vbi_process_ttx(struct upipe *upipe, struct uref *uref,
                                       const uint8_t *r, size_t size, int line)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    assert(size == 56);
    printf(".\n");
    if (unlikely(upipe_vbi->telx_uref == NULL))
        upipe_vbi_alloc_telx(upipe, uref);
    if (unlikely(upipe_vbi->telx_uref == NULL))
        return;

    if (unlikely(upipe_vbi->telx_w >= upipe_vbi->telx_end)) {
        upipe_warn(upipe, "too many teletext units");
        return;
    }

    upipe_vbi->telx_w[0] = DVBVBI_ID_TTX_SUB;
    upipe_vbi->telx_w[1] = DVBVBI_LENGTH;
    dvbvbittx_set_field(&upipe_vbi->telx_w[2], line > 313);
    dvbvbittx_set_line(&upipe_vbi->telx_w[2], (line % 313) );
    upipe_vbi->telx_w[3] = TELX_FRAMING_CODE;

    for (int j = 0; j < 42; j++)
        upipe_vbi->telx_w[4 + j] = REVERSE(r[j] & 0xff);

    upipe_vbi->telx_w += DVBVBI_LENGTH + DVBVBI_UNIT_HEADER_SIZE;
}

/** @internal @This allocates an uref for cea708 data.
 *
 * @param upipe description structure of the pipe
 * @param uref incoming uref structure
 */
static void upipe_vbi_alloc_cea708(struct upipe *upipe, struct uref *uref)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    struct upipe_vbi_output *upipe_vbi_cea708 =
        upipe_vbi_to_cea708_subpipe(upipe_vbi);
    struct ubuf *ubuf = ubuf_block_alloc(upipe_vbi_cea708->ubuf_mgr,
                                         MAX_TELX_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_vbi->cea708_uref = uref_fork(uref, ubuf);
    if (unlikely(upipe_vbi->cea708_uref == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    int size = -1;
    uint8_t *w;
    if (unlikely(!ubase_check(uref_block_write(upipe_vbi->cea708_uref, 0,
                                               &size, &w)))) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_vbi->cea708_uref);
        upipe_vbi->cea708_uref = NULL;
        return;
    }

    upipe_vbi->cea708_begin = w;
    upipe_vbi->cea708_w = w;
    upipe_vbi->cea708_end = w + size;
}

/** @internal @This completes an uref for cea708 data, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_vbi_output_cea708(struct upipe *upipe,
                                     struct upump **upump_p)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    struct upipe_vbi_output *upipe_vbi_cea708 =
        upipe_vbi_to_cea708_subpipe(upipe_vbi);

    uref_block_unmap(upipe_vbi->cea708_uref, 0);
    uref_block_resize(upipe_vbi->cea708_uref, 0,
                      upipe_vbi->cea708_w - upipe_vbi->cea708_begin);

    upipe_vbi_output_output(upipe_vbi_output_to_upipe(upipe_vbi_cea708),
                             upipe_vbi->cea708_uref, upump_p);
    upipe_vbi->cea708_uref = NULL;
}

/** @internal @This processes a cea708 structure.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param r pointer to the structure
 * @param size size of the structure
 */
static void upipe_vbi_process_cea708(struct upipe *upipe, struct uref *uref,
                                      const uint16_t *r, size_t size)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    if (unlikely(upipe_vbi->cea708_uref == NULL))
        upipe_vbi_alloc_cea708(upipe, uref);
    if (unlikely(upipe_vbi->cea708_uref == NULL))
        return;
    if (unlikely(size > upipe_vbi->cea708_end - upipe_vbi->cea708_w)) {
        upipe_warn(upipe, "too much cea708 data");
        return;
    }

    for (int j = 0; j < size; j++)
        *upipe_vbi->cea708_w++ = r[j] & 0xff;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return true if the uref was processed
 */
static bool upipe_vbi_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);
    struct upipe_vbi_output *upipe_vbi_ttx =
        upipe_vbi_to_ttx_subpipe(upipe_vbi);
    struct upipe_vbi_output *upipe_vbi_cea708 =
        upipe_vbi_to_cea708_subpipe(upipe_vbi);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uref_free(upipe_vbi->flow_def);
        upipe_vbi->flow_def = uref;
        uref_pic_flow_get_fps(uref, &upipe_vbi->fps);

        /* Update subpipes' flow_def. */
        upipe_vbi_output_build_flow_def(
                upipe_vbi_output_to_upipe(upipe_vbi_ttx));
        upipe_vbi_output_build_flow_def(
                upipe_vbi_output_to_upipe(upipe_vbi_cea708));
        return true;
    }

    if (upipe_vbi_ttx->ubuf_mgr == NULL ||
        upipe_vbi_cea708->ubuf_mgr == NULL)
        return false;

    /* Now process frames. */
    size_t hsize, vsize, stride;
    const uint8_t *r;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 !ubase_check(uref_pic_plane_size(uref, "y8", &stride,
                                                  NULL, NULL, NULL)) ||
                 !ubase_check(uref_pic_plane_read(uref, "y8", 0, 0, -1, -1,
                                                  &r)))) {
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return true;
    }

    upipe_vbi->vbi_dec.bytes_per_line = stride;

    vbi_sliced out[49];
    memset(out, 0, sizeof(out));
    int n = vbi_raw_decode(&upipe_vbi->vbi_dec, (uint8_t*)r, out);
    if (n) {
        for (int i = 0; i < n; i++) {
            printf("decoded line %d: %s : %x\n",
                    out[i].line,
                    vbi_sliced_name(out[i].id),
                    out[i].id
                    );

            if (out[i].id & VBI_SLICED_TELETEXT_B) {
                upipe_vbi_process_ttx(upipe, uref, out[i].data,
                        sizeof(out[i].data), out[i].line);
            }
        }
    } else {
        printf("not decoded\n");
    }

    uref_pic_plane_unmap(uref, "y8", 0, 0, -1, -1);
    uref_free(uref);

    if (upipe_vbi->telx_uref != NULL)
        upipe_vbi_output_telx(upipe, upump_p);
    if (upipe_vbi->cea708_uref != NULL)
        upipe_vbi_output_cea708(upipe, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_vbi_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_vbi_check_input(upipe)) {
        upipe_vbi_hold_input(upipe, uref);
        upipe_vbi_block_input(upipe, upump_p);
    } else if (!upipe_vbi_handle(upipe, uref, upump_p)) {
        upipe_vbi_hold_input(upipe, uref);
        upipe_vbi_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_vbi_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    UBASE_RETURN(uref_pic_flow_match_macropixel(flow_def, 1, 1))
    UBASE_RETURN(uref_pic_flow_match_planes(flow_def, 1, 1))
    UBASE_RETURN(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8"))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    uref_pic_flow_clear_format(flow_def_dup);
    UBASE_RETURN(uref_flow_set_def(flow_def_dup, "void."))
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_vbi_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_vbi_set_flow_def(upipe, flow_def);
        }

        case UPIPE_VBI_GET_TTX_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VBI_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_vbi_output_to_upipe(
                    upipe_vbi_to_ttx_subpipe(
                        upipe_vbi_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_VBI_GET_CEA708_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VBI_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_vbi_output_to_upipe(
                    upipe_vbi_to_cea708_subpipe(
                        upipe_vbi_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vbi_free(struct upipe *upipe)
{
    struct upipe_vbi *upipe_vbi = upipe_vbi_from_upipe(upipe);

    upipe_vbi_output_clean(upipe_vbi_output_to_upipe(
                upipe_vbi_to_ttx_subpipe(upipe_vbi)));
    upipe_vbi_output_clean(upipe_vbi_output_to_upipe(
                upipe_vbi_to_cea708_subpipe(upipe_vbi)));

    vbi_raw_decoder_destroy(&upipe_vbi->vbi_dec);

    upipe_throw_dead(upipe);

    upipe_vbi_clean_input(upipe);
    upipe_vbi_clean_urefcount(upipe);

    upipe_clean(upipe);
    free(upipe_vbi);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_vbi_mgr = {
    .refcount = NULL,
    .signature = UPIPE_VBI_SIGNATURE,

    .upipe_alloc = _upipe_vbi_alloc,
    .upipe_input = upipe_vbi_input,
    .upipe_control = upipe_vbi_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for vbi pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_vbi_mgr_alloc(void)
{
    return &upipe_vbi_mgr;
}

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
 * @short Upipe filter processing vertical ancillary data
 *
 * Normative references:
 *  - SMPTE 291M-2006 (ancillary data packet and space formatting)
 *  - SMPTE RP 291-2009 (assigned ancillary identification codes)
 *  - SMPTE 2016-1-2007 (active format description and bar data)
 *  - SMPTE 2016-3-2007 (VANC mapping of AFD and bar data)
 *  - SMPTE 334-1-2007 (VANC mapping of caption data)
 *  - SMPTE RDD-08 (teletext subtitles for HDTV/OP-47)
 *  - ETSI EN 300 775 V1.2.1 (2003-05) (carriage of VBI in DVB)
 *  - ETSI EN 300 743 V1.3.1 (2003-01) (teletext in DVB)
 */

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
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
#include <upipe-filters/upipe_filter_vanc.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

#include <bitstream/smpte/291.h>
#include <bitstream/smpte/2010.h>
#include <bitstream/smpte/rdd08.h>
#include <bitstream/dvb/vbi.h>
#include <bitstream/dvb/telx.h>

/** from my dice */
#define MAX_SCTE104_SIZE 4096
/** 34 telx frames of 46 octets */
#define MAX_TELX_SIZE (1 + 34 * 46)
/** from my dice */
#define MAX_CEA708_SIZE 4096

/** @internal @This is the private context of an output of a vanc pipe */
struct upipe_vanc_output {
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
static int upipe_vanc_output_check(struct upipe *upipe,
                                   struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_vanc_output, upipe, UPIPE_VANC_OUTPUT_SIGNATURE)
UPIPE_HELPER_OUTPUT(upipe_vanc_output, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_vanc_output, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_vanc_output_check,
                      upipe_vanc_output_register_output_request,
                      upipe_vanc_output_unregister_output_request)

/** @internal upipe_vanc private structure */
struct upipe_vanc {
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
    /** afd subpipe */
    struct upipe_vanc_output afd_subpipe;
    /** scte104 subpipe */
    struct upipe_vanc_output scte104_subpipe;
    /** op47 subpipe */
    struct upipe_vanc_output op47_subpipe;
    /** cea708 subpipe */
    struct upipe_vanc_output cea708_subpipe;

    /* input data */
    /** input flow_def */
    struct uref *flow_def;
    /** Frame rate */
    struct urational fps;

    /* AFD data */
    /** AFD if not UINT8_MAX */
    uint8_t afd;
    /* bar data if AFD not UINT8_MAX */
    uint8_t bar_data[5];

    /* SCTE-104 data */
    /** uref to be sent at the end of this packet */
    struct uref *scte104_uref;
    /** beginning of the buffer */
    uint8_t *scte104_begin;
    /** current write position */
    uint8_t *scte104_w;
    /** maximum write position */
    uint8_t *scte104_end;
    /** currently signalled octet rate */
    uint64_t scte104_octetrate;

    /* OP47 data */
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
static void upipe_vanc_resume(struct upipe *upipe);
/** @hidden */
static bool upipe_vanc_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_vanc, upipe, UPIPE_VANC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_vanc, urefcount, upipe_vanc_free)
UPIPE_HELPER_INPUT(upipe_vanc, urefs, nb_urefs, max_urefs, blockers, upipe_vanc_handle)

UBASE_FROM_TO(upipe_vanc, upipe_mgr, sub_mgr, sub_mgr)
UBASE_FROM_TO(upipe_vanc, upipe_vanc_output, afd_subpipe, afd_subpipe)
UBASE_FROM_TO(upipe_vanc, upipe_vanc_output, scte104_subpipe, scte104_subpipe)
UBASE_FROM_TO(upipe_vanc, upipe_vanc_output, op47_subpipe, op47_subpipe)
UBASE_FROM_TO(upipe_vanc, upipe_vanc_output, cea708_subpipe, cea708_subpipe)

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

/** @internal @This initializes an output subpipe of a vanc pipe.
 *
 * @param upipe pointer to subpipe
 * @param sub_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_vanc_output_init(struct upipe *upipe,
        struct upipe_mgr *sub_mgr, struct uprobe *uprobe)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_sub_mgr(sub_mgr);
    upipe_init(upipe, sub_mgr, uprobe);
    upipe->refcount = &upipe_vanc->urefcount;

    upipe_vanc_output_init_output(upipe);
    upipe_vanc_output_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_vanc_output_check(struct upipe *upipe,
                                   struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_vanc_output_store_flow_def(upipe, flow_format);

    struct upipe_vanc_output *upipe_vanc_output =
        upipe_vanc_output_from_upipe(upipe);
    if (upipe_vanc_output->ubuf_mgr == NULL)
        return UBASE_ERR_NONE;

    struct upipe_vanc *upipe_vanc = upipe_vanc_from_sub_mgr(upipe->mgr);
    upipe_vanc_resume(upipe_vanc_to_upipe(upipe_vanc));
    return UBASE_ERR_NONE;
}

/** @internal @This builds the flow definition of the output subpipe.
 *
 * @param upipe pointer to subpipe
 */
static void upipe_vanc_output_build_flow_def(struct upipe *upipe)
{
    struct upipe_vanc_output *upipe_vanc_output =
        upipe_vanc_output_from_upipe(upipe);
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_sub_mgr(upipe->mgr);
    struct uref *flow_def = uref_dup(upipe_vanc->flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    if (upipe_vanc_output == upipe_vanc_to_afd_subpipe(upipe_vanc)) {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, "void.afd."))
        if (upipe_vanc->afd != UINT8_MAX) {
            UBASE_ERROR(upipe, uref_pic_flow_set_afd(flow_def, upipe_vanc->afd))
            UBASE_ERROR(upipe, uref_pic_flow_set_bar(flow_def,
                        upipe_vanc->bar_data, sizeof(upipe_vanc->bar_data)))
        }
        upipe_vanc_output_store_flow_def(upipe, flow_def);

    } else if (upipe_vanc_output == upipe_vanc_to_scte104_subpipe(upipe_vanc)) {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, "block.scte104.scte35.void."))
        upipe_vanc_output_require_ubuf_mgr(upipe, flow_def);

    } else if (upipe_vanc_output == upipe_vanc_to_op47_subpipe(upipe_vanc)) {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, "block.dvb_teletext.pic.sub."))
        if (upipe_vanc->telx_octetrate)
            UBASE_ERROR(upipe, uref_block_flow_set_octetrate(flow_def,
                        upipe_vanc->telx_octetrate))

        upipe_vanc_output_require_ubuf_mgr(upipe, flow_def);

    } else {
        UBASE_ERROR(upipe, uref_flow_set_def(flow_def, "block.cea708.pic.sub."))
        upipe_vanc_output_require_ubuf_mgr(upipe, flow_def);
    }
}

/** @internal @This processes control commands on a vanc output pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_vanc_output_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_vanc_output_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_vanc_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_vanc_output_set_output(upipe, output);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_vanc_to_upipe(upipe_vanc_from_sub_mgr(upipe->mgr));
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
static void upipe_vanc_output_clean(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_vanc_output_clean_ubuf_mgr(upipe);
    upipe_vanc_output_clean_output(upipe);

    upipe_clean(upipe);
}

/** @internal @This initializes the output manager for a vanc pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vanc_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_vanc->sub_mgr;
    sub_mgr->refcount = NULL;
    sub_mgr->signature = UPIPE_VANC_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = NULL;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_vanc_output_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a vanc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_vanc_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    if (signature != UPIPE_VANC_SIGNATURE)
        return NULL;
    struct uprobe *uprobe_afd = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_scte104 = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_op47 = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_cea708 = va_arg(args, struct uprobe *);

    struct upipe_vanc *upipe_vanc = malloc(sizeof(struct upipe_vanc));
    if (unlikely(upipe_vanc == NULL)) {
        uprobe_release(uprobe_op47);
        uprobe_release(uprobe_cea708);
        return NULL;
    }

    struct upipe *upipe = upipe_vanc_to_upipe(upipe_vanc);
    upipe_init(upipe, mgr, uprobe);

    upipe_vanc_init_urefcount(upipe);
    upipe_vanc_init_input(upipe);
    upipe_vanc_init_sub_mgr(upipe);
    upipe_vanc->flow_def = NULL;
    upipe_vanc->fps.num = 25;
    upipe_vanc->fps.den = 1;
    upipe_vanc->afd = UINT8_MAX;
    upipe_vanc->scte104_uref = NULL;
    upipe_vanc->telx_uref = NULL;
    upipe_vanc->telx_octetrate = 0;
    upipe_vanc->cea708_uref = NULL;

    upipe_vanc_output_init(upipe_vanc_output_to_upipe(
                                upipe_vanc_to_afd_subpipe(upipe_vanc)),
                              &upipe_vanc->sub_mgr, uprobe_afd);
    upipe_vanc_output_init(upipe_vanc_output_to_upipe(
                                upipe_vanc_to_scte104_subpipe(upipe_vanc)),
                              &upipe_vanc->sub_mgr, uprobe_scte104);
    upipe_vanc_output_init(upipe_vanc_output_to_upipe(
                                upipe_vanc_to_op47_subpipe(upipe_vanc)),
                              &upipe_vanc->sub_mgr, uprobe_op47);
    upipe_vanc_output_init(upipe_vanc_output_to_upipe(
                                upipe_vanc_to_cea708_subpipe(upipe_vanc)),
                              &upipe_vanc->sub_mgr, uprobe_cea708);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This resumes the vanc pipe by processing the input queue.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_vanc_resume(struct upipe *upipe)
{
    bool was_buffered = !upipe_vanc_check_input(upipe);
    upipe_vanc_output_input(upipe);
    upipe_vanc_unblock_input(upipe);
    if (was_buffered && upipe_vanc_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_vanc_input. */
        upipe_release(upipe);
    }
}

/** @internal @This processes an afd structure.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param r pointer to the structure
 * @param size size of the structure
 */
static void upipe_vanc_process_afd(struct upipe *upipe, struct uref *uref,
                                   const uint16_t *r, size_t size)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_afd =
        upipe_vanc_to_afd_subpipe(upipe_vanc);
    if (unlikely(size < 8)) {
        upipe_warn_va(upipe, "AFD data is too small (%zu)", size);
        return;
    }
    upipe_vanc->afd = r[0] & 0xff;
    for (unsigned int i = 0; i < 5; i++)
        upipe_vanc->bar_data[i] = r[3 + i] & 0xff;

    upipe_vanc_output_build_flow_def(upipe_vanc_output_to_upipe(upipe_vanc_afd));
}

/** @internal @This allocates an uref for scte104 data.
 *
 * @param upipe description structure of the pipe
 * @param uref incoming uref structure
 */
static void upipe_vanc_alloc_scte104(struct upipe *upipe, struct uref *uref)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_scte104 =
        upipe_vanc_to_scte104_subpipe(upipe_vanc);
    struct ubuf *ubuf = ubuf_block_alloc(upipe_vanc_scte104->ubuf_mgr,
                                         MAX_SCTE104_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_vanc->scte104_uref = uref_fork(uref, ubuf);
    if (unlikely(upipe_vanc->scte104_uref == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    int size = -1;
    uint8_t *w;
    if (unlikely(!ubase_check(uref_block_write(upipe_vanc->scte104_uref, 0,
                                               &size, &w)))) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_vanc->scte104_uref);
        upipe_vanc->scte104_uref = NULL;
        return;
    }

    upipe_vanc->scte104_begin = w;
    upipe_vanc->scte104_w = w;
    upipe_vanc->scte104_end = w + size;
}

/** @internal @This completes an uref for scte104 data, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_vanc_output_scte104(struct upipe *upipe,
                                      struct upump **upump_p)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_scte104 =
        upipe_vanc_to_scte104_subpipe(upipe_vanc);

    uref_block_unmap(upipe_vanc->scte104_uref, 0);
    uref_block_resize(upipe_vanc->scte104_uref, 0,
                      upipe_vanc->scte104_w - upipe_vanc->scte104_begin);

    upipe_vanc_output_output(upipe_vanc_output_to_upipe(upipe_vanc_scte104),
                             upipe_vanc->scte104_uref, upump_p);
    upipe_vanc->scte104_uref = NULL;
}

/** @internal @This processes a scte104 structure.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @param r pointer to the structure
 * @param size size of the structure
 */
static void upipe_vanc_process_scte104(struct upipe *upipe, struct uref *uref,
                                       struct upump **upump_p,
                                       const uint16_t *r, size_t size)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_scte104 =
        upipe_vanc_to_scte104_subpipe(upipe_vanc);

    if (unlikely(s2010_get_version(r) != S2010_VERSION)) {
        upipe_warn_va(upipe, "invalid SMPTE 2010 version (%"PRIu8")",
                      s2010_get_version(r));
        return;
    }

    if (!s2010_get_following(r) && upipe_vanc->scte104_uref != NULL)
        upipe_vanc_output_scte104(upipe, upump_p);

    if (unlikely(upipe_vanc->scte104_uref == NULL))
        upipe_vanc_alloc_scte104(upipe, uref);
    if (unlikely(upipe_vanc->scte104_uref == NULL))
        return;
    if (unlikely(size - 1 > upipe_vanc->scte104_end - upipe_vanc->scte104_w)) {
        upipe_warn(upipe, "too much scte104 data");
        return;
    }

    for (int j = 1; j < size; j++)
        *upipe_vanc->scte104_w++ = r[j] & 0xff;
}

/** @internal @This allocates an uref for telx data.
 *
 * @param upipe description structure of the pipe
 * @param uref incoming uref structure
 */
static void upipe_vanc_alloc_telx(struct upipe *upipe, struct uref *uref)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_op47 =
        upipe_vanc_to_op47_subpipe(upipe_vanc);
    struct ubuf *ubuf = ubuf_block_alloc(upipe_vanc_op47->ubuf_mgr,
                                         MAX_TELX_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_vanc->telx_uref = uref_fork(uref, ubuf);
    if (unlikely(upipe_vanc->telx_uref == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    int size = -1;
    uint8_t *w;
    if (unlikely(!ubase_check(uref_block_write(upipe_vanc->telx_uref, 0,
                                               &size, &w)))) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_vanc->telx_uref);
        upipe_vanc->telx_uref = NULL;
        return;
    }

    w[0] = DVBVBI_DATA_IDENTIFIER;
    upipe_vanc->telx_begin = w;
    upipe_vanc->telx_w = w + 1;
    upipe_vanc->telx_end = w + size;
}

/** @internal @This completes an uref for telx data, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_vanc_output_telx(struct upipe *upipe,
                                   struct upump **upump_p)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_op47 =
        upipe_vanc_to_op47_subpipe(upipe_vanc);

    unsigned int nb_units =
        (upipe_vanc->telx_w - (upipe_vanc->telx_begin + 1)) /
        (DVBVBI_LENGTH + DVBVBI_UNIT_HEADER_SIZE);
    unsigned int nb_units_rounded = 3 + (nb_units / 4) * 4;

    while (nb_units < nb_units_rounded) {
        upipe_vanc->telx_w[0] = DVBVBI_ID_STUFFING;
        upipe_vanc->telx_w[1] = DVBVBI_LENGTH;
        memset(&upipe_vanc->telx_w[2], 0xff, DVBVBI_LENGTH);

        upipe_vanc->telx_w += DVBVBI_LENGTH + DVBVBI_UNIT_HEADER_SIZE;
        nb_units++;
    }
    uref_block_unmap(upipe_vanc->telx_uref, 0);
    uref_block_resize(upipe_vanc->telx_uref, 0,
                      upipe_vanc->telx_w - upipe_vanc->telx_begin);

    /* Adjust flow def */
    uint64_t octetrate =
        ((upipe_vanc->telx_w - upipe_vanc->telx_begin) * upipe_vanc->fps.num +
         upipe_vanc->fps.den - 1) / upipe_vanc->fps.den;
    if (upipe_vanc->telx_octetrate != octetrate) {
        upipe_vanc->telx_octetrate = octetrate;
        upipe_vanc_output_build_flow_def(
                upipe_vanc_output_to_upipe(upipe_vanc_op47));
    }

    upipe_vanc_output_output(upipe_vanc_output_to_upipe(upipe_vanc_op47),
                             upipe_vanc->telx_uref, upump_p);
    upipe_vanc->telx_uref = NULL;
}

/** @internal @This processes an OP47 SDP structure.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param r pointer to the structure
 * @param size size of the structure
 */
static void upipe_vanc_process_op47sdp(struct upipe *upipe, struct uref *uref,
                                       const uint16_t *r, size_t size)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    if (size < RDD08SDP_HEADER_SIZE + RDD08SDP_FOOTER_SIZE) {
        upipe_warn_va(upipe, "OP47 SDP data is too small (%zu)", size);
        return;
    }
    if (unlikely(r[0] != RDD08SDP_IDENT1 || r[1] != RDD08SDP_IDENT2)) {
        upipe_warn(upipe, "invalid OP47 SDP data");
        return;
    }

    uint8_t length = rdd08sdp_get_length(r);
    if (unlikely(size < length)) {
        upipe_warn_va(upipe, "OP47 SDP length is too big (%zu)", length);
        return;
    }

    uint16_t format = rdd08sdp_get_format(r);
    uint16_t footer = rdd08sdp_get_footer(r);
    if (unlikely(format != RDD08SDP_FORMAT_WST ||
                 footer != RDD08SDP_FOOTER)) {
        upipe_warn(upipe, "unknown OP47 SDP format");
        return;
    }

    if (unlikely(!rdd08sdp_check_cs(r))) {
        upipe_warn(upipe, "invalid OP47 SDP checksum");
        return;
    }
    if (unlikely(!rdd08sdp_validate(r))) {
        upipe_warn(upipe, "invalid OP47 SDP syntax");
        return;
    }
    if (unlikely(!rdd08sdp_get_a(r, 0))) {
        upipe_warn(upipe, "invalid OP47 A struct");
        return;
    }

    if (unlikely(upipe_vanc->telx_uref == NULL))
        upipe_vanc_alloc_telx(upipe, uref);
    if (unlikely(upipe_vanc->telx_uref == NULL))
        return;

    uint8_t n = 0;
    while (n < 5) {
        uint8_t a = rdd08sdp_get_a(r, n);
        if (!a)
            break;
        if (unlikely(upipe_vanc->telx_w >= upipe_vanc->telx_end)) {
            upipe_warn(upipe, "too many teletext units");
            break;
        }

        const uint16_t *b = rdd08sdp_get_b(r, n);
        if (unlikely(b[2] != RDD08SDP_FRAMING_CODE))
            upipe_warn(upipe, "invalid teletext unit");

        upipe_vanc->telx_w[0] = DVBVBI_ID_TTX_SUB;
        upipe_vanc->telx_w[1] = DVBVBI_LENGTH;
        dvbvbittx_set_field(&upipe_vanc->telx_w[2], rdd08sdpa_get_field(a));
        dvbvbittx_set_line(&upipe_vanc->telx_w[2], rdd08sdpa_get_line(a));
        upipe_vanc->telx_w[3] = TELX_FRAMING_CODE;

        for (int j = 0; j < RDD08SDP_B_SIZE - 3; j++)
            upipe_vanc->telx_w[4 + j] = REVERSE(b[3 + j] & 0xff);

        upipe_vanc->telx_w += DVBVBI_LENGTH + DVBVBI_UNIT_HEADER_SIZE;
        n++;
    }
}

/** @internal @This allocates an uref for cea708 data.
 *
 * @param upipe description structure of the pipe
 * @param uref incoming uref structure
 */
static void upipe_vanc_alloc_cea708(struct upipe *upipe, struct uref *uref)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_cea708 =
        upipe_vanc_to_cea708_subpipe(upipe_vanc);
    struct ubuf *ubuf = ubuf_block_alloc(upipe_vanc_cea708->ubuf_mgr,
                                         MAX_TELX_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_vanc->cea708_uref = uref_fork(uref, ubuf);
    if (unlikely(upipe_vanc->cea708_uref == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    int size = -1;
    uint8_t *w;
    if (unlikely(!ubase_check(uref_block_write(upipe_vanc->cea708_uref, 0,
                                               &size, &w)))) {
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        uref_free(upipe_vanc->cea708_uref);
        upipe_vanc->cea708_uref = NULL;
        return;
    }

    upipe_vanc->cea708_begin = w;
    upipe_vanc->cea708_w = w;
    upipe_vanc->cea708_end = w + size;
}

/** @internal @This completes an uref for cea708 data, and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to upump structure
 */
static void upipe_vanc_output_cea708(struct upipe *upipe,
                                     struct upump **upump_p)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_cea708 =
        upipe_vanc_to_cea708_subpipe(upipe_vanc);

    uref_block_unmap(upipe_vanc->cea708_uref, 0);
    uref_block_resize(upipe_vanc->cea708_uref, 0,
                      upipe_vanc->cea708_w - upipe_vanc->cea708_begin);

    upipe_vanc_output_output(upipe_vanc_output_to_upipe(upipe_vanc_cea708),
                             upipe_vanc->cea708_uref, upump_p);
    upipe_vanc->cea708_uref = NULL;
}

/** @internal @This processes a cea708 structure.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param r pointer to the structure
 * @param size size of the structure
 */
static void upipe_vanc_process_cea708(struct upipe *upipe, struct uref *uref,
                                      const uint16_t *r, size_t size)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    if (unlikely(upipe_vanc->cea708_uref == NULL))
        upipe_vanc_alloc_cea708(upipe, uref);
    if (unlikely(upipe_vanc->cea708_uref == NULL))
        return;
    if (unlikely(size > upipe_vanc->cea708_end - upipe_vanc->cea708_w)) {
        upipe_warn(upipe, "too much cea708 data");
        return;
    }

    for (int j = 0; j < size; j++)
        *upipe_vanc->cea708_w++ = r[j] & 0xff;
}

/** @internal @This processes a vanc line.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @param r pointer to line buffer
 * @param hsize size of the line in horizontal pixels
 */
static void upipe_vanc_process_line(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p,
                                    const uint16_t *r, size_t hsize)
{
    while (hsize > S291_HEADER_SIZE + S291_FOOTER_SIZE) {
        if (r[0] != S291_ADF1 || r[1] != S291_ADF2 || r[2] != S291_ADF3) {
            r++;
            hsize--;
            continue;
        }

        uint8_t did = s291_get_did(r);
        uint8_t sdid = s291_get_sdid(r);
        uint8_t dc = s291_get_dc(r);
        if (S291_HEADER_SIZE + dc + S291_FOOTER_SIZE > hsize) {
            upipe_warn_va(upipe, "ancillary too large (%"PRIu8" > %zu) for 0x%"PRIx8"/0x%"PRIx8,
                          dc, hsize, did, sdid);
            break;
        }

        if (!s291_check_cs(r)) {
            upipe_warn_va(upipe, "invalid CRC for 0x%"PRIx8"/0x%"PRIx8,
                          did, sdid);
            r += 3;
            hsize -= 3;
            continue;
        }
        r += S291_HEADER_SIZE;
        hsize -= S291_HEADER_SIZE;

        if (did == S291_AFD_DID && sdid == S291_AFD_SDID)
            upipe_vanc_process_afd(upipe, uref, r, dc);
        else if (did == S291_SCTE104_DID && sdid == S291_SCTE104_SDID)
            upipe_vanc_process_scte104(upipe, uref, upump_p, r, dc);
        else if (did == S291_OP47SDP_DID && sdid == S291_OP47SDP_SDID)
            upipe_vanc_process_op47sdp(upipe, uref, r, dc);
        else if (did == S291_CEA708_DID && sdid == S291_CEA708_SDID)
            upipe_vanc_process_cea708(upipe, uref, r, dc);
        else
            upipe_verbose_va(upipe, "unhandled ancillary 0x%"PRIx8"/0x%"PRIx8,
                             did, sdid);

        r += dc + S291_FOOTER_SIZE;
        hsize -= dc + S291_FOOTER_SIZE;
    }
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 * @return true if the uref was processed
 */
static bool upipe_vanc_handle(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);
    struct upipe_vanc_output *upipe_vanc_afd =
        upipe_vanc_to_afd_subpipe(upipe_vanc);
    struct upipe_vanc_output *upipe_vanc_scte104 =
        upipe_vanc_to_scte104_subpipe(upipe_vanc);
    struct upipe_vanc_output *upipe_vanc_op47 =
        upipe_vanc_to_op47_subpipe(upipe_vanc);
    struct upipe_vanc_output *upipe_vanc_cea708 =
        upipe_vanc_to_cea708_subpipe(upipe_vanc);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uref_free(upipe_vanc->flow_def);
        upipe_vanc->flow_def = uref;
        uref_pic_flow_get_fps(uref, &upipe_vanc->fps);

        /* Update subpipes' flow_def. */
        upipe_vanc_output_build_flow_def(
                upipe_vanc_output_to_upipe(upipe_vanc_afd));
        upipe_vanc_output_build_flow_def(
                upipe_vanc_output_to_upipe(upipe_vanc_scte104));
        upipe_vanc_output_build_flow_def(
                upipe_vanc_output_to_upipe(upipe_vanc_op47));
        upipe_vanc_output_build_flow_def(
                upipe_vanc_output_to_upipe(upipe_vanc_cea708));
        return true;
    }

    if (upipe_vanc_scte104->ubuf_mgr == NULL ||
        upipe_vanc_op47->ubuf_mgr == NULL ||
        upipe_vanc_cea708->ubuf_mgr == NULL)
        return false;

    /* Now process frames. */
    size_t hsize, vsize, stride;
    const uint8_t *r;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 !ubase_check(uref_pic_plane_size(uref, "x10", &stride,
                                                  NULL, NULL, NULL)) ||
                 !ubase_check(uref_pic_plane_read(uref, "x10", 0, 0, -1, -1,
                                                  &r)))) {
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return true;
    }

    for (unsigned int i = 0; i < vsize; i++) {
        upipe_vanc_process_line(upipe, uref, upump_p, (uint16_t *)r, hsize);
        r += stride;
    }

    uref_pic_plane_unmap(uref, "x10", 0, 0, -1, -1);
    uref_free(uref);

    if (upipe_vanc->scte104_uref != NULL)
        upipe_vanc_output_scte104(upipe, upump_p);
    if (upipe_vanc->telx_uref != NULL)
        upipe_vanc_output_telx(upipe, upump_p);
    if (upipe_vanc->cea708_uref != NULL)
        upipe_vanc_output_cea708(upipe, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_vanc_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_vanc_check_input(upipe)) {
        upipe_vanc_hold_input(upipe, uref);
        upipe_vanc_block_input(upipe, upump_p);
    } else if (!upipe_vanc_handle(upipe, uref, upump_p)) {
        upipe_vanc_hold_input(upipe, uref);
        upipe_vanc_block_input(upipe, upump_p);
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
static int upipe_vanc_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    UBASE_RETURN(uref_pic_flow_match_macropixel(flow_def, 1, 1))
    UBASE_RETURN(uref_pic_flow_match_planes(flow_def, 1, 1))
    UBASE_RETURN(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "x10"))
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
static int upipe_vanc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_vanc_set_flow_def(upipe, flow_def);
        }

        case UPIPE_VANC_GET_AFD_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VANC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_vanc_output_to_upipe(
                    upipe_vanc_to_afd_subpipe(
                        upipe_vanc_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_VANC_GET_SCTE104_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VANC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_vanc_output_to_upipe(
                    upipe_vanc_to_scte104_subpipe(
                        upipe_vanc_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_VANC_GET_OP47_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VANC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_vanc_output_to_upipe(
                    upipe_vanc_to_op47_subpipe(
                        upipe_vanc_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_VANC_GET_CEA708_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_VANC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_vanc_output_to_upipe(
                    upipe_vanc_to_cea708_subpipe(
                        upipe_vanc_from_upipe(upipe)));
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
static void upipe_vanc_free(struct upipe *upipe)
{
    struct upipe_vanc *upipe_vanc = upipe_vanc_from_upipe(upipe);

    upipe_vanc_output_clean(upipe_vanc_output_to_upipe(
                upipe_vanc_to_afd_subpipe(upipe_vanc)));
    upipe_vanc_output_clean(upipe_vanc_output_to_upipe(
                upipe_vanc_to_scte104_subpipe(upipe_vanc)));
    upipe_vanc_output_clean(upipe_vanc_output_to_upipe(
                upipe_vanc_to_op47_subpipe(upipe_vanc)));
    upipe_vanc_output_clean(upipe_vanc_output_to_upipe(
                upipe_vanc_to_cea708_subpipe(upipe_vanc)));

    upipe_throw_dead(upipe);

    upipe_vanc_clean_input(upipe);
    upipe_vanc_clean_urefcount(upipe);

    upipe_clean(upipe);
    free(upipe_vanc);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_vanc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_VANC_SIGNATURE,

    .upipe_alloc = _upipe_vanc_alloc,
    .upipe_input = upipe_vanc_input,
    .upipe_control = upipe_vanc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for glx_input pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_vanc_mgr_alloc(void)
{
    return &upipe_vanc_mgr;
}

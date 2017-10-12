/*
 * Copyright (C) 2015-2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *          Rafaël Carré
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe module to encapsulate data in SMPTE 337
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe-modules/upipe_s337_encaps.h>

#include <bitstream/atsc/a52.h>
#include <bitstream/smpte/337.h>

#define EXPECTED_FLOW_DEF "block.ac3.sound."

/** upipe_s337_encaps structure */
struct upipe_s337_encaps {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_s337_encaps_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static bool upipe_s337_encaps_handle(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_s337_encaps, upipe, UPIPE_S337E_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_s337_encaps, urefcount, upipe_s337_encaps_free)
UPIPE_HELPER_VOID(upipe_s337_encaps);
UPIPE_HELPER_OUTPUT(upipe_s337_encaps, output, flow_def, output_state, request_list);
UPIPE_HELPER_INPUT(upipe_s337_encaps, urefs, nb_urefs, max_urefs, blockers, upipe_s337_encaps_handle)
UPIPE_HELPER_UBUF_MGR(upipe_s337_encaps, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_s337_encaps_check,
                      upipe_s337_encaps_register_output_request,
                      upipe_s337_encaps_unregister_output_request)

static int upipe_s337_encaps_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_s337_encaps_store_flow_def(upipe, flow_format);

    bool was_buffered = !upipe_s337_encaps_check_input(upipe);
    upipe_s337_encaps_output_input(upipe);
    upipe_s337_encaps_unblock_input(upipe);
    if (was_buffered && upipe_s337_encaps_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_s337_encaps_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_s337_encaps_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    if (!upipe_s337_encaps_check_input(upipe)) {
        upipe_s337_encaps_hold_input(upipe, uref);
        upipe_s337_encaps_block_input(upipe, upump_p);
    } else if (!upipe_s337_encaps_handle(upipe, uref, upump_p)) {
        upipe_s337_encaps_hold_input(upipe, uref);
        upipe_s337_encaps_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static bool upipe_s337_encaps_handle(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_s337_encaps *upipe_s337_encaps = upipe_s337_encaps_from_upipe(upipe);

    if (!upipe_s337_encaps->ubuf_mgr)
        return false;

    size_t block_size;
    if (!ubase_check(uref_block_size(uref, &block_size))) {
        upipe_err(upipe, "Couldn't read block size");
        uref_free(uref);
        return true;
    }

    struct ubuf *ubuf = ubuf_sound_alloc(upipe_s337_encaps->ubuf_mgr, A52_FRAME_SAMPLES);
    if (!ubuf)
        return false;

    if (block_size / 2 > A52_FRAME_SAMPLES - 4) {
        upipe_err_va(upipe, "AC-3 block size %d too big", block_size);
        block_size = (A52_FRAME_SAMPLES - 4) * 2;
    }

    int32_t *out_data;
    ubuf_sound_write_int32_t(ubuf, 0, -1, &out_data, 1);

    /* Pa, Pb, Pc, Pd */
    out_data[0] = (S337_PREAMBLE_A1 << 24) | (S337_PREAMBLE_A2 << 16);
    out_data[1] = (S337_PREAMBLE_B1 << 24) | (S337_PREAMBLE_B2 << 16);
    out_data[2] = (S337_TYPE_A52 << 16) | (S337_MODE_16 << 21) |
        (S337_TYPE_A52_REP_RATE_FLAG << 24);
    out_data[3] = ((block_size * 8) & 0xffff) << 16;

    int offset = 0;
    while (block_size) {
        const uint8_t *buf;
        int size = block_size;

        if (!ubase_check(uref_block_read(uref, offset, &size, &buf)) || size == 0) {
            ubuf_sound_unmap(ubuf, 0, -1, 1);
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }

        for (int i = 0; i < size/2; i++)
            out_data[4 + offset/2 + i] = (buf[2*i + 0] << 24) | (buf[2*i + 1] << 16);

        uref_block_unmap(uref, offset);

        block_size -= size;
        offset += size;
    }

    memset(&out_data[4 + offset/2], 0, 4 * (A52_FRAME_SAMPLES*2 - 4 - offset/2));

    ubuf_sound_unmap(ubuf, 0, -1, 1);

    uref_attach_ubuf(uref, ubuf);
    upipe_s337_encaps_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_s337_encaps_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))

    uint64_t rate;
    UBASE_RETURN(uref_sound_flow_get_rate(flow_def, &rate))

    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup = uref_dup(flow_def);
    uref_flow_set_def(flow_def_dup, "sound.s32.s337.a52.");
    uref_sound_flow_set_channels(flow_def_dup, 2);
    uref_sound_flow_set_sample_size(flow_def_dup, 2*4);

    if (flow_def_dup == NULL)
        return UBASE_ERR_ALLOC;

    if (!ubase_check(uref_sound_flow_add_plane(flow_def_dup, "lr")) ||
        !ubase_check(uref_sound_flow_set_rate(flow_def_dup, rate))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_ALLOC;
    }

    upipe_s337_encaps_require_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a s337_encaps pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_s337_encaps_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_s337_encaps_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_s337_encaps_free_output_proxy(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_s337_encaps_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_s337_encaps_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a s337_encaps pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_s337_encaps_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_s337_encaps_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_s337_encaps *upipe_s337_encaps =
        upipe_s337_encaps_from_upipe(upipe);
    upipe_s337_encaps_init_urefcount(upipe);
    upipe_s337_encaps_init_output(upipe);
    upipe_s337_encaps_init_input(upipe);
    upipe_s337_encaps_init_ubuf_mgr(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_s337_encaps_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_s337_encaps_clean_output(upipe);
    upipe_s337_encaps_clean_input(upipe);
    upipe_s337_encaps_clean_urefcount(upipe);
    upipe_s337_encaps_clean_ubuf_mgr(upipe);
    upipe_s337_encaps_free_void(upipe);
}

static struct upipe_mgr upipe_s337_encaps_mgr = {
    .refcount = NULL,
    .signature = UPIPE_S337E_SIGNATURE,

    .upipe_alloc = upipe_s337_encaps_alloc,
    .upipe_input = upipe_s337_encaps_input,
    .upipe_control = upipe_s337_encaps_control
};

/** @This returns the management structure for s337_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s337_encaps_mgr_alloc(void)
{
    return &upipe_s337_encaps_mgr;
}

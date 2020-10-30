/*
 * Pad picture urefs with blank space.
 *
 * Copyright (c) 2020 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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
 * @short Upipe module to pad picture urefs with blank space.
 */

#include <upipe/ubase.h>
#include <upipe/upipe.h>
#include <upipe/uprobe.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>

#include <upipe-modules/upipe_pad.h>

/** we only accept pictures */
#define EXPECTED_FLOW_DEF "pic."

struct upipe_pad {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;

    /* flow_def given at alloc describing how to pad the picture */
    uint64_t w, h, l, r, t, b;
};

/** @hidden */
static bool upipe_pad_handle(struct upipe *upipe, struct uref *uref, struct upump **upump_p);
/** @hidden */
static int upipe_pad_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_pad, upipe, UPIPE_PAD_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_pad, urefcount, upipe_pad_free);
UPIPE_HELPER_FLOW(upipe_pad, EXPECTED_FLOW_DEF);
UPIPE_HELPER_INPUT(upipe_pad, urefs, nb_urefs, max_urefs, blockers, upipe_pad_handle)
UPIPE_HELPER_OUTPUT(upipe_pad, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_pad, ubuf_mgr, flow_format, ubuf_mgr_request, upipe_pad_check,
        upipe_pad_register_output_request, upipe_pad_unregister_output_request)

/** @internal @This allocates a pad pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_pad_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_pad_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_pad *upipe_pad = upipe_pad_from_upipe(upipe);

    upipe_pad_init_urefcount(upipe);
    upipe_pad_init_input(upipe);
    upipe_pad_init_output(upipe);
    upipe_pad_init_ubuf_mgr(upipe);

    /* TODO: support more options than only the pad attributes. */

    uint64_t l = 0, r = 0, t = 0, b = 0;
    if (!ubase_check(uref_pic_get_lpadding(flow_def, &l))
            || !ubase_check(uref_pic_get_rpadding(flow_def, &r))
            || !ubase_check(uref_pic_get_tpadding(flow_def, &t))
            || !ubase_check(uref_pic_get_bpadding(flow_def, &b)))
        return NULL;
    upipe_pad->l = l; upipe_pad->r = r; upipe_pad->t = t; upipe_pad->b = b;
    upipe_pad->w = upipe_pad->h = 0;

    uref_free(flow_def);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees a pad pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_pad_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_pad_clean_urefcount(upipe);
    upipe_pad_clean_input(upipe);
    upipe_pad_clean_output(upipe);
    upipe_pad_clean_ubuf_mgr(upipe);
    upipe_pad_free_flow(upipe);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_pad_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_pad *upipe_pad = upipe_pad_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));

    uint64_t w, h;
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &w));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &h));
    upipe_pad->w = w += upipe_pad->l + upipe_pad->r;
    upipe_pad->h = h += upipe_pad->t + upipe_pad->b;

    struct uref *flow_def_dst = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dst);
    UBASE_RETURN(uref_pic_flow_set_hsize(flow_def_dst, w));
    UBASE_RETURN(uref_pic_flow_set_vsize(flow_def_dst, h));

    upipe_pad_require_ubuf_mgr(upipe, flow_def_dst);
    return UBASE_ERR_NONE;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_pad_handle(struct upipe *upipe, struct uref *uref, struct upump **upump_p)
{
    struct upipe_pad *upipe_pad = upipe_pad_from_upipe(upipe);
    if (!upipe_pad->ubuf_mgr) {
        upipe_warn(upipe, "no ubuf_mgr, holding input uref");
        return false;
    }

    struct ubuf *ubuf_dst = ubuf_pic_alloc(upipe_pad->ubuf_mgr, upipe_pad->w, upipe_pad->h);
    if (!ubuf_dst) {
        uref_free(uref);
        upipe_throw_error(upipe, UBASE_ERR_ALLOC);
        return true;
    }
    ubuf_pic_clear(ubuf_dst, 0, 0, -1, -1, 1);

    uint64_t w = upipe_pad->w - upipe_pad->l - upipe_pad->r;
    uint64_t h = upipe_pad->h - upipe_pad->t - upipe_pad->b;
    UBASE_ERROR(upipe, ubuf_pic_blit(ubuf_dst, uref->ubuf, upipe_pad->l, upipe_pad->t,
                0, 0, w, h, 0, 0));

    uref_attach_ubuf(uref, ubuf_dst);
    upipe_pad_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_pad_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_pad *upipe_pad = upipe_pad_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_pad_store_flow_def(upipe, flow_format);
    if (upipe_pad->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_pad_check_input(upipe);
    upipe_pad_output_input(upipe);
    upipe_pad_unblock_input(upipe);
    if (was_buffered && upipe_pad_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_pad_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_pad_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    if (!upipe_pad_check_input(upipe)) {
        upipe_pad_hold_input(upipe, uref);
        upipe_pad_block_input(upipe, upump_p);
    } else if (!upipe_pad_handle(upipe, uref, upump_p)) {
        upipe_pad_hold_input(upipe, uref);
        upipe_pad_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This handles the pipe control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_pad_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_pad_control_ubuf_mgr(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_pad_control_output(upipe, command, args));
    switch (command) {
        default:
            return UBASE_ERR_UNHANDLED;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_pad_set_flow_def(upipe, flow);
        }
    }
}

/** @internal @This is the static pipe manager. */
static struct upipe_mgr upipe_pad_mgr = {
    .refcount = NULL,
    .signature = UPIPE_PAD_SIGNATURE,
    .upipe_alloc = upipe_pad_alloc,
    .upipe_input = upipe_pad_input,
    .upipe_control = upipe_pad_control,
};

/** @This returns the pad pipe manager.
 *
 * @return a pointer to the pipe manager
 */
struct upipe_mgr *upipe_pad_mgr_alloc(void)
{
    return &upipe_pad_mgr;
}

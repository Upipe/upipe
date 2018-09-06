/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
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
 * @short Upipe module to join chunks into one picture
 */

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_input.h>

#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>

#include <upipe-modules/upipe_row_join.h>

struct upipe_row_join {
    /** refcount management structure */
    struct urefcount urefcount;
    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
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
    struct uchain input_urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** public upipe structure */
    struct upipe upipe;

    struct uref *output_uref;
    uint64_t output_width, output_height;
    uint64_t output_duration;
};

/** @hidden */
static int upipe_row_join_check(struct upipe *upipe, struct uref *flow_format);
/** @hidden */
static bool upipe_row_join_handle(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_row_join, upipe, UPIPE_ROW_JOIN_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_row_join, urefcount, upipe_row_join_free)
UPIPE_HELPER_VOID(upipe_row_join)
UPIPE_HELPER_OUTPUT(upipe_row_join, output, flow_def, output_state,
                    request_list)
UPIPE_HELPER_UBUF_MGR(upipe_row_join, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_row_join_check,
                      upipe_row_join_register_output_request,
                      upipe_row_join_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_row_join, input_urefs, nb_urefs, max_urefs, blockers,
        upipe_row_join_handle)

static int upipe_row_join_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format)
        upipe_row_join_store_flow_def(upipe, flow_format);

    bool was_buffered = !upipe_row_join_check_input(upipe);
    upipe_row_join_output_input(upipe);
    upipe_row_join_unblock_input(upipe);
    if (was_buffered && upipe_row_join_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_row_join_input. */
        upipe_release(upipe);
    }

    return UBASE_ERR_NONE;
}


static int upipe_row_join_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_row_join *ctx = upipe_row_join_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    upipe_row_join_require_ubuf_mgr(upipe, flow_def);

    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &ctx->output_width));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &ctx->output_height));
    struct urational fps;
    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &fps));
    ctx->output_duration = fps.den * UCLOCK_FREQ / fps.num;

    return UBASE_ERR_NONE;
}

static int upipe_row_join_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_row_join_set_flow_def(upipe, flow_def);
        }
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_row_join_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_row_join_free(struct upipe *upipe)
{
    upipe_row_join_clean_ubuf_mgr(upipe);
    upipe_row_join_clean_urefcount(upipe);
    upipe_row_join_clean_output(upipe);
    upipe_row_join_clean_input(upipe);
    upipe_row_join_free_void(upipe);
}

static struct upipe *upipe_row_join_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct upipe *upipe =
        upipe_row_join_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_row_join *upipe_row_join = upipe_row_join_from_upipe(upipe);

    upipe_row_join->output_uref = NULL;

    upipe_row_join_init_input(upipe);
    upipe_row_join_init_output(upipe);
    upipe_row_join_init_ubuf_mgr(upipe);
    upipe_row_join_init_urefcount(upipe);

    return upipe;
}

static void upipe_row_join_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    if (!upipe_row_join_check_input(upipe)) {
        upipe_row_join_hold_input(upipe, uref);
        upipe_row_join_block_input(upipe, upump_p);
    } else if (!upipe_row_join_handle(upipe, uref, upump_p)) {
        upipe_row_join_hold_input(upipe, uref);
        upipe_row_join_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

static bool upipe_row_join_handle(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p)
{
    struct upipe_row_join *ctx = upipe_row_join_from_upipe(upipe);
    if (!ctx->ubuf_mgr)
        return false;

    /* TODO: check errors, sanity checks. */

    uint64_t vpos;
    if (!ubase_check(uref_pic_get_vposition(uref, &vpos))) {
        upipe_verbose(upipe, "dropping picture with no vposition");
        uref_free(uref);
        return true;
    }

    if (vpos == 0) {
        /* Top of frame so allocate new output. */
        if (ctx->output_uref) {
            /* Output a stored uref, if any. */
            upipe_row_join_output(upipe, ctx->output_uref, NULL);
            ctx->output_uref = NULL;
        }

        /* Allocate a new output picture. */
        struct ubuf *ubuf = ubuf_pic_alloc(ctx->ubuf_mgr, ctx->output_width, ctx->output_height);
        if (unlikely(!ubuf)) {
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return true;
        }

        /* Allocate new output uref. */
        ctx->output_uref = uref_fork(uref, ubuf);
        if (unlikely(!ctx->output_uref)) {
            upipe_throw_error(upipe, UBASE_ERR_ALLOC);
            ubuf_free(ubuf);
            uref_free(uref);
            return true;
        }

        /* Correct attributes on the new uref. */
        uref_pic_delete_vposition(ctx->output_uref);
        uref_clock_set_duration(ctx->output_uref, ctx->output_duration);
    }

    size_t width, height;
    uref_pic_size(uref, &width, &height, NULL);
    int ret = ubuf_pic_blit(ctx->output_uref->ubuf, uref->ubuf, 0, vpos, 0, 0, width, height, 0, 0);
    if (ret) {
        upipe_throw_error(upipe, ret);
        uref_free(uref);
        return true;
    }

    vpos += height;
    if (vpos == ctx->output_height) {
        upipe_row_join_output(upipe, ctx->output_uref, NULL);
        ctx->output_uref = NULL;
    }

    uref_free(uref);
    return true;
}

static struct upipe_mgr upipe_row_join_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ROW_JOIN_SIGNATURE,

    .upipe_alloc = upipe_row_join_alloc,
    .upipe_input = upipe_row_join_input,
    .upipe_control = upipe_row_join_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_row_join_mgr_alloc(void)
{
    return &upipe_row_join_mgr;
}

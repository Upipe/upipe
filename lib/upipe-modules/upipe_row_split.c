/*
 * Copyright (C) 2017-2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe module to split pictures into a series of rows
 */

#include <stdlib.h>
#include <limits.h>

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_flow_def.h>

#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>

#include <upipe-modules/upipe_row_split.h>

struct upipe_row_split {
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

    /** frame duration in ticks */
    uint64_t frame_duration;

    /** chunk height */
    uint64_t chunk_height;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_row_split, upipe, UPIPE_ROW_SPLIT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_row_split, urefcount, upipe_row_split_free)
UPIPE_HELPER_FLOW(upipe_row_split, NULL)
UPIPE_HELPER_OUTPUT(upipe_row_split, output, flow_def, output_state,
                    request_list)

static int upipe_row_split_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_row_split *upipe_row_split = upipe_row_split_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    uint64_t hsize;
    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &hsize));

    if (hsize < upipe_row_split->chunk_height) {
        upipe_err_va(upipe, "chunk height too big (%" PRIu64 " > %" PRIu64 ")",
                hsize, upipe_row_split->chunk_height);
        return UBASE_ERR_INVALID;
    }

    struct urational fps;
    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &fps));
    upipe_row_split->frame_duration = UCLOCK_FREQ * fps.den / fps.num;

    upipe_row_split_store_flow_def(upipe, uref_dup(flow_def));

    return UBASE_ERR_NONE;
}

static int upipe_row_split_control(struct upipe *upipe, int command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_row_split_set_flow_def(upipe, flow_def);
        }
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_row_split_control_output(upipe, command, args);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static void upipe_row_split_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_row_split_clean_urefcount(upipe);
    upipe_row_split_clean_output(upipe);
    upipe_row_split_free_flow(upipe);
}

static struct upipe *upipe_row_split_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature,
                                           va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe =
        upipe_row_split_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_row_split *upipe_row_split = upipe_row_split_from_upipe(upipe);

    if (flow_def == NULL || !ubase_check(uref_pic_flow_get_vsize(flow_def,
                    &upipe_row_split->chunk_height))) {
        upipe_err(upipe, "Missing chunk height");
        upipe_clean(upipe);
        uref_free(flow_def);
        free(upipe_row_split);
        return NULL;
    }
    uref_free(flow_def);

    upipe_row_split_init_urefcount(upipe);
    upipe_row_split_init_output(upipe);

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_row_split_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_row_split *upipe_row_split = upipe_row_split_from_upipe(upipe);

    size_t hsize, vsize;
    uint8_t macropixel;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize,
                        &macropixel)))) {
        upipe_warn(upipe, "dropping picture");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return;
    }

    uint64_t vsize_chunk = upipe_row_split->chunk_height;

    for (uint64_t done = 0; done < vsize; done += vsize_chunk) {
        if (done + vsize_chunk > vsize)
            vsize_chunk = vsize - done;

        struct ubuf *ubuf = ubuf_dup(uref->ubuf);
        if (!ubuf)
            break;

        if (!ubase_check(ubuf_pic_resize(ubuf, 0, done, -1, vsize_chunk))) {
            upipe_err(upipe, "Could not resize picture");
            ubuf_free(ubuf);
            break;
        }

        struct uref *uref_chunk = uref_fork(uref, ubuf);

        uref_pic_set_original_height(uref_chunk, vsize);
        uref_clock_set_duration(uref_chunk,
                (vsize_chunk * upipe_row_split->frame_duration) / vsize);

        uref_pic_set_vposition(uref_chunk, done);

        uint64_t delay = (done * upipe_row_split->frame_duration) / vsize;
        uref_clock_add_date_sys(uref_chunk, delay);
        uref_clock_add_date_prog(uref_chunk, delay);
        uref_clock_add_date_orig(uref_chunk, delay);

        upipe_row_split_output(upipe, uref_chunk, NULL);
    }

    uref_free(uref);
}

static struct upipe_mgr upipe_row_split_mgr = {
    .refcount = NULL,
    .signature = UPIPE_ROW_SPLIT_SIGNATURE,

    .upipe_alloc = upipe_row_split_alloc,
    .upipe_input = upipe_row_split_input,
    .upipe_control = upipe_row_split_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_row_split_mgr_alloc(void)
{
    return &upipe_row_split_mgr;
}

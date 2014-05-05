/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe ebur128
 */

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-filters/upipe_filter_ebur128.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

#include "ebur128/ebur128.h"

/** @internal upipe_filter_ebur128 private structure */
struct upipe_filter_ebur128 {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    /** has flow def been sent ?*/
    bool output_flow_sent;

    /** ebur128 state */
    ebur128_state *st;

    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_filter_ebur128, upipe,
                   UPIPE_FILTER_EBUR128_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_filter_ebur128, urefcount,
                       upipe_filter_ebur128_free)
UPIPE_HELPER_VOID(upipe_filter_ebur128)
UPIPE_HELPER_OUTPUT(upipe_filter_ebur128, output,
                    output_flow, output_flow_sent)

/** @internal @This allocates a filter pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_filter_ebur128_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature,
                                                va_list args)
{
    struct upipe *upipe = upipe_filter_ebur128_alloc_void(mgr, uprobe, signature,
                                                        args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);
    upipe_filter_ebur128->st = NULL;

    upipe_filter_ebur128_init_urefcount(upipe);
    upipe_filter_ebur128_init_output(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_filter_ebur128_input(struct upipe *upipe, struct uref *uref,
                                       struct upump **upump_p)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);
    double peak0 = 0, peak1 = 0, loud = 0;
    size_t samples;
    if (unlikely(!ubase_check(uref_sound_size(uref, &samples, NULL)))) {
        upipe_warn(upipe, "invalid sound buffer");
        uref_free(uref);
        return;
    }
    const char *channel = NULL;
    const uint8_t *buf = NULL;
    if (ubase_check(uref_sound_plane_iterate(uref, &channel)) && channel) {
        if (unlikely(!ubase_check(uref_sound_plane_read_uint8_t(uref,
                channel, 0, -1, &buf)))) {
            upipe_warn(upipe, "error mapping sound buffer");
            uref_free(uref);
            return;
        }

        if (unlikely((uintptr_t)buf & 1)) {
            upipe_warn(upipe, "unaligned buffer");
        }
        ebur128_add_frames_short(upipe_filter_ebur128->st,
                                 (const short*) buf, samples);
        uref_sound_plane_unmap(uref, channel, 0, -1);
    }
    ebur128_loudness_momentary(upipe_filter_ebur128->st, &loud);
    ebur128_sample_peak(upipe_filter_ebur128->st, 0, &peak0);
    ebur128_sample_peak(upipe_filter_ebur128->st, 1, &peak1);

    //upipe_verbose_va(upipe, "loud %f \tsample peak %f \t%f", loud, peak0, peak1);
    upipe_verbose_va(upipe, "loud %f", loud);

    upipe_filter_ebur128_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow flow definition packet
 * @return an error code
 */
static int upipe_filter_ebur128_set_flow_def(struct upipe *upipe,
                                             struct uref *flow)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);
    if (flow == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow, "sound."))
    uint8_t channels;
    uint64_t rate;
    if (unlikely(!ubase_check(uref_sound_flow_get_rate(flow, &rate))
            || !ubase_check(uref_sound_flow_get_channels(flow, &channels)))) {
        return UBASE_ERR_INVALID;
    }

    struct uref *flow_dup;
    if (unlikely((flow_dup = uref_dup(flow)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (unlikely(upipe_filter_ebur128->st)) {
        ebur128_destroy(&upipe_filter_ebur128->st);
        ebur128_change_parameters(upipe_filter_ebur128->st, channels, rate);
    } else {
    upipe_filter_ebur128->st = ebur128_init(channels, rate,
                                            EBUR128_MODE_SAMPLE_PEAK);
    }

    upipe_filter_ebur128_store_flow_def(upipe, flow_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_filter_ebur128_control(struct upipe *upipe,
                                        int command, va_list args)
{
    switch (command) {
        case UPIPE_AMEND_FLOW_FORMAT:
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_filter_ebur128_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_filter_ebur128_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_filter_ebur128_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_filter_ebur128_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_filter_ebur128_free(struct upipe *upipe)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);
    if (likely(upipe_filter_ebur128->st)) {
        ebur128_destroy(&upipe_filter_ebur128->st);
    }
    upipe_throw_dead(upipe);

    upipe_filter_ebur128_clean_output(upipe);
    upipe_filter_ebur128_clean_urefcount(upipe);
    upipe_filter_ebur128_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_filter_ebur128_mgr = {
    .refcount = NULL,
    .signature = UPIPE_FILTER_EBUR128_SIGNATURE,

    .upipe_alloc = upipe_filter_ebur128_alloc,
    .upipe_input = upipe_filter_ebur128_input,
    .upipe_control = upipe_filter_ebur128_control
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_ebur128_mgr_alloc(void)
{
    return &upipe_filter_ebur128_mgr;
}

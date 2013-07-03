/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/uref_block.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-filters/upipe_filter_ebur128.h>

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>

#include "ebur128/ebur128.h"

/** @internal upipe_filter_ebur128 private structure */
struct upipe_filter_ebur128 {
    /** ebur128 state */
    ebur128_state *st;

    /** output */
    struct upipe *output;
    /** output flow */
    struct uref *output_flow;
    /** has flow def been sent ?*/
    bool output_flow_sent;
    /** public structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_filter_ebur128, upipe);
UPIPE_HELPER_FLOW(upipe_filter_ebur128, "block.")
UPIPE_HELPER_OUTPUT(upipe_filter_ebur128, output, output_flow, output_flow_sent)

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
                                              uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_filter_ebur128_alloc_flow(mgr,
                           uprobe, signature, args, &flow_def);
    if (unlikely(upipe == NULL)) {
        return NULL;
    }
    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);

    /* FIXME hardcoded parameters */
    upipe_filter_ebur128->st = ebur128_init(2, 48000, EBUR128_MODE_SAMPLE_PEAK);

    upipe_filter_ebur128_init_output(upipe);
    upipe_filter_ebur128_store_flow_def(upipe, flow_def);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_filter_ebur128_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_filter_ebur128 *upipe_filter_ebur128 =
                                 upipe_filter_ebur128_from_upipe(upipe);
    size_t size = 0;
    uint64_t samples = 0;
    const uint8_t *buf = 0;
    int bufsize = -1;
    double peak0, peak1, loud;

    if (unlikely(!uref->ubuf)) { // no ubuf in uref
        upipe_filter_ebur128_output(upipe, uref, upump);
        return;
    }
    if (unlikely(!uref_block_size(uref, &size))) {
        upipe_warn(upipe, "could not get block size");
        uref_free(uref);
        return;
    }
    if (unlikely(!uref_sound_flow_get_samples(uref, &samples))) {
        upipe_warn(upipe, "could not get samples");
        uref_free(uref);
        return;
    }
    if (unlikely(!uref_block_read(uref, 0, &bufsize, &buf))) {
        upipe_warn(upipe, "could not map buffer");
        uref_free(uref);
        return;
    }
    if (unlikely(((uintptr_t)buf & 1) || (bufsize != size))) {
        upipe_warn_va(upipe, "error mapping samples (%p, %zu, %d)",
                      buf, size, bufsize);
    }

    ebur128_add_frames_short(upipe_filter_ebur128->st,
                             (const short *) buf, (size_t) samples);
    uref_block_unmap(uref, 0);

    /* get sample peak */
    ebur128_loudness_momentary(upipe_filter_ebur128->st, &loud);
    ebur128_sample_peak(upipe_filter_ebur128->st, 0, &peak0);
    ebur128_sample_peak(upipe_filter_ebur128->st, 1, &peak1);

    upipe_notice_va(upipe, "loud %f \tsample peak %f \t%f", loud, peak0, peak1);
    
    upipe_filter_ebur128_output(upipe, uref, upump);
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_filter_ebur128_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_filter_ebur128_get_flow_def(upipe, p);
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
            return false;
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
    upipe_filter_ebur128_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_filter_ebur128_mgr = {
    .signature = UPIPE_FILTER_EBUR128_SIGNATURE,
    .upipe_alloc = upipe_filter_ebur128_alloc,
    .upipe_input = upipe_filter_ebur128_input,
    .upipe_control = upipe_filter_ebur128_control,
    .upipe_free = upipe_filter_ebur128_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_ebur128_mgr_alloc(void)
{
    return &upipe_filter_ebur128_mgr;
}

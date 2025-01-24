/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 * Copyright (C) 2024 EasyTools
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
 * @short Upipe module to aggregate complete packets up to specified MTU
 */

#include "upipe/ubase.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/ubuf.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_output_size.h"
#include "upipe-modules/upipe_aggregate.h"

#include <stdlib.h>
#include <stdarg.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** default output size, corresponding to 7 TS packets */
#define DEFAULT_MTU 1316

/** @internal @This is the private context of a agg pipe. */
struct upipe_agg {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** MTU */
    size_t output_size;
    /** incoming buffer size */
    size_t input_size;

    /** current segmented aggregation */
    struct uref *aggregated;
    /** current stored size */
    size_t size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_agg, upipe, UPIPE_AGG_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_agg, urefcount, upipe_agg_free)
UPIPE_HELPER_VOID(upipe_agg)
UPIPE_HELPER_OUTPUT(upipe_agg, output, flow_def, output_state, request_list)
UPIPE_HELPER_OUTPUT_SIZE(upipe_agg, output_size)

/** @internal @This allocates a agg pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_agg_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_agg_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_agg *upipe_agg = upipe_agg_from_upipe(upipe);
    upipe_agg_init_urefcount(upipe);
    upipe_agg_init_output(upipe);
    upipe_agg_init_output_size(upipe, DEFAULT_MTU);
    upipe_agg->input_size = 0;
    upipe_agg->size = 0;
    upipe_agg->aggregated = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This flushes the aggregated buffer.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_agg_flush(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_agg *upipe_agg = upipe_agg_from_upipe(upipe);
    struct uref *uref = upipe_agg->aggregated;
    upipe_agg->aggregated = NULL;
    if (uref)
        upipe_agg_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition for real.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_agg_set_flow_def_real(struct upipe *upipe,
                                       struct uref *flow_def)
{
    struct upipe_agg *upipe_agg = upipe_agg_from_upipe(upipe);
    uint64_t size = 0;
    uint64_t octetrate = 0;
    uint64_t latency = 0;

    uref_block_flow_get_size(flow_def, &size);
    uref_block_flow_get_octetrate(flow_def, &octetrate);
    uref_clock_get_latency(flow_def, &latency);

    upipe_agg->input_size = size;

    int err = uref_block_flow_set_size(flow_def, upipe_agg->output_size);
    if (unlikely(!ubase_check(err))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, err);
        return err;
    }

    if (octetrate) {
        latency += (uint64_t)upipe_agg->output_size * UCLOCK_FREQ / octetrate;
        err = uref_clock_set_latency(flow_def, latency);
        if (unlikely(!ubase_check(err))) {
            uref_free(flow_def);
            upipe_throw_fatal(upipe, err);
            return err;
        }
    }
    upipe_agg_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}


/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_agg_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    struct upipe_agg *upipe_agg = upipe_agg_from_upipe(upipe);
    size_t size = 0;
    const size_t output_size = upipe_agg->output_size;

    if (unlikely(ubase_check(uref_flow_get_def(uref, NULL)))) {
        upipe_agg_flush(upipe, upump_p);
        upipe_agg_set_flow_def_real(upipe, uref);
        return;
    }

    uref_block_size(uref, &size);

    /* check for invalid or too large size */
    if (unlikely(size == 0 || size > output_size)) {
        upipe_warn_va(upipe,
            "received packet of invalid size: %zu (output_size == %zu)", size, output_size);
        uref_free(uref);
        return;
    }

    /* flush if incoming packet makes aggregated overflow */
    if (upipe_agg->size + size > output_size)
        upipe_agg_flush(upipe, upump_p);

    /* keep or attach incoming packet */
    if (unlikely(!upipe_agg->aggregated)) {
        upipe_agg->aggregated = uref;
        upipe_agg->size = size;
    } else {
        struct ubuf *append = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!ubase_check(uref_block_append(upipe_agg->aggregated,
                                                    append)))) {
            upipe_warn(upipe, "error appending packet");
            ubuf_free(append);
            return;
        };
        upipe_agg->size += size;
    }

    /* anticipate next packet size and flush now if necessary */
    if (upipe_agg->input_size)
        size = upipe_agg->input_size;
    if (unlikely(upipe_agg->size + size > output_size))
        upipe_agg_flush(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_agg_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    upipe_agg_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static int upipe_agg_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_agg_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_agg_control_output_size(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_agg_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_agg_free(struct upipe *upipe)
{
    struct upipe_agg *upipe_agg = upipe_agg_from_upipe(upipe);

    if (unlikely(upipe_agg->aggregated)) {
        upipe_agg_output(upipe, upipe_agg->aggregated, NULL);
    }
    upipe_throw_dead(upipe);
    upipe_agg_clean_output(upipe);
    upipe_agg_clean_output_size(upipe);
    upipe_agg_clean_urefcount(upipe);
    upipe_agg_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_agg_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AGG_SIGNATURE,

    .upipe_alloc = upipe_agg_alloc,
    .upipe_input = upipe_agg_input,
    .upipe_control = upipe_agg_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all agg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_agg_mgr_alloc(void)
{
    return &upipe_agg_mgr;
}

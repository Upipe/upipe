/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module to aggregate complete TS packets up to specified MTU
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_aggregate.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegts"

/** @internal @This is the private context of a ts_aggregate pipe. */
struct upipe_ts_aggregate {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** MTU */
    size_t mtu;

    /** current segmented aggregation */
    struct uref *aggregated;
    /** current stored size */
    size_t size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_aggregate, upipe)

UPIPE_HELPER_OUTPUT(upipe_ts_aggregate, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_aggregate pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_aggregate_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe)
{
    struct upipe_ts_aggregate *upipe_ts_aggregate =
        malloc(sizeof(struct upipe_ts_aggregate));
    if (unlikely(upipe_ts_aggregate == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_aggregate_to_upipe(upipe_ts_aggregate);
    upipe_init(upipe, mgr, uprobe);
    upipe_ts_aggregate_init_output(upipe);
    upipe_ts_aggregate->mtu = 7 * TS_SIZE;
    upipe_ts_aggregate->size = 0;
    upipe_ts_aggregate->aggregated = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_aggregate_input_block(struct upipe *upipe,
                                           struct uref *uref, struct upump *upump)
{
    struct upipe_ts_aggregate *upipe_ts_aggregate = upipe_ts_aggregate_from_upipe(upipe);
    size_t size = 0;
    const size_t mtu = upipe_ts_aggregate->mtu;
    struct ubuf *append;

    uref_block_size(uref, &size);

    /* check for invalid or too large size */
    if (unlikely(size == 0 || size > mtu)) {
        upipe_warn_va(upipe,
            "received packet of invalid size: %zu (mtu == %zu)", size, mtu);
        uref_free(uref);
        return;
    }

    /* flush if incoming packet makes aggregated overflow */
    if (unlikely(upipe_ts_aggregate->size + size > mtu)) {
        upipe_ts_aggregate_output(upipe, upipe_ts_aggregate->aggregated, upump);
        upipe_ts_aggregate->aggregated = NULL;
    }

    /* keep or attach incoming packet */
    if (unlikely(!upipe_ts_aggregate->aggregated)) {
        upipe_ts_aggregate->aggregated = uref;
        upipe_ts_aggregate->size = size;
    } else {
        append = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!uref_block_append(upipe_ts_aggregate->aggregated, append))) {
            upipe_warn(upipe, "error appending packet");
            ubuf_free(append);
            return;
        };
        upipe_ts_aggregate->size += size;
    }

    /* anticipate next packet size and flush now if necessary */
    if (unlikely(upipe_ts_aggregate->size + size > mtu)) {
        upipe_ts_aggregate_output(upipe, upipe_ts_aggregate->aggregated, upump);
        upipe_ts_aggregate->aggregated = NULL;
        upipe_ts_aggregate->size = 0;
    }
}


/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_aggregate_input(struct upipe *upipe, struct uref *uref,
                                     struct upump *upump)
{
    struct upipe_ts_aggregate *upipe_ts_aggregate = upipe_ts_aggregate_from_upipe(upipe);
    const char *def;

    /* flow definition */
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            upipe_ts_aggregate_store_flow_def(upipe, NULL);
            upipe_throw_flow_def_error(upipe, uref);
            uref_free(uref);
            return;
        }

        upipe_dbg_va(upipe, "flow definition: %s", def);
        upipe_ts_aggregate_store_flow_def(upipe, uref);
        return;
    }

    /* flow end */
    if (unlikely(uref_flow_get_end(uref))) {
        /* flush current aggregated packet */
        if (unlikely(upipe_ts_aggregate->aggregated)) {
            upipe_ts_aggregate_output(upipe,
                    upipe_ts_aggregate->aggregated, upump);
            upipe_ts_aggregate->aggregated = NULL;
        }
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(upipe_ts_aggregate->flow_def == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_ts_aggregate_input_block(upipe, uref, upump);
}

/** @internal @This returns the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @return false in case of error
 */
static bool _upipe_ts_aggregate_get_mtu(struct upipe *upipe, int *mtu_p)
{
    struct upipe_ts_aggregate *upipe_ts_aggregate = upipe_ts_aggregate_from_upipe(upipe);
    assert(mtu_p != NULL);
    *mtu_p = upipe_ts_aggregate->mtu;
    return true;
}

/** @internal @This sets the configured mtu.
 * @param upipe description structure of the pipe
 * @param mtu configured mtu, in octets
 * @return false in case of error
 */
static bool _upipe_ts_aggregate_set_mtu(struct upipe *upipe, int mtu)
{
    struct upipe_ts_aggregate *upipe_ts_aggregate = upipe_ts_aggregate_from_upipe(upipe);
    if (mtu < 0)
        return false;
    upipe_ts_aggregate->mtu = mtu;
    return true;
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_aggregate_control(struct upipe *upipe,
                                   enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_aggregate_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_aggregate_set_output(upipe, output);
        }

        case UPIPE_TS_AGGREGATE_GET_MTU: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_AGGREGATE_SIGNATURE);
            int *mtu_p = va_arg(args, int *);
            return _upipe_ts_aggregate_get_mtu(upipe, mtu_p);
        }
        case UPIPE_TS_AGGREGATE_SET_MTU: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_AGGREGATE_SIGNATURE);
            int mtu = va_arg(args, int);
            return _upipe_ts_aggregate_set_mtu(upipe, mtu);
        }
        default:
            return false;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aggregate_free(struct upipe *upipe)
{
    struct upipe_ts_aggregate *upipe_ts_aggregate = upipe_ts_aggregate_from_upipe(upipe);

    /* in case we are freed before receiving flow end */
    if (unlikely(upipe_ts_aggregate->aggregated)) {
        upipe_ts_aggregate_output(upipe, upipe_ts_aggregate->aggregated, NULL);
    }
    upipe_throw_dead(upipe);
    upipe_ts_aggregate_clean_output(upipe);

    upipe_clean(upipe);
    free(upipe_ts_aggregate);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_aggregate_mgr = {
    .signature = UPIPE_TS_AGGREGATE_SIGNATURE,

    .upipe_alloc = upipe_ts_aggregate_alloc,
    .upipe_input = upipe_ts_aggregate_input,
    .upipe_control = upipe_ts_aggregate_control,
    .upipe_free = upipe_ts_aggregate_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_aggregate pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_aggregate_mgr_alloc(void)
{
    return &upipe_ts_aggregate_mgr;
}

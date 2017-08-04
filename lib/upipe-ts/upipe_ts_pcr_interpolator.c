/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe module interpolating timestamps from PCRs
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_pcr_interpolator.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegts."

/** @internal @This is the private context of a ts_pcr_interpolator pipe. */
struct upipe_ts_pcr_interpolator {
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

    /** previous PCR value */
    uint64_t last_pcr;

    /** number of TS packets since last PCR */
    unsigned int packets;

    /** number of TS packets between the last 2 PCRs */
    unsigned int pcr_packets;

    /** delta between the last 2 PCRs */
    uint64_t pcr_delta;

    /** if next packet output should show discontinuity */
    bool discontinuity;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pcr_interpolator, upipe, UPIPE_TS_PCR_INTERPOLATOR_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_pcr_interpolator, urefcount, upipe_ts_pcr_interpolator_free)
UPIPE_HELPER_VOID(upipe_ts_pcr_interpolator)
UPIPE_HELPER_OUTPUT(upipe_ts_pcr_interpolator, output, flow_def, output_state, request_list)

/** @internal @This allocates a ts_pcr_interpolator pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pcr_interpolator_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_pcr_interpolator_alloc_void(mgr, uprobe, signature,
                                                     args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_pcr_interpolator *upipe_ts_pcr_interpolator = upipe_ts_pcr_interpolator_from_upipe(upipe);
    upipe_ts_pcr_interpolator_init_urefcount(upipe);
    upipe_ts_pcr_interpolator_init_output(upipe);
    upipe_ts_pcr_interpolator->last_pcr = 0;
    upipe_ts_pcr_interpolator->packets = 0;
    upipe_ts_pcr_interpolator->pcr_packets = 0;
    upipe_ts_pcr_interpolator->pcr_delta = 0;
    upipe_ts_pcr_interpolator->discontinuity = true;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This interpolates the PCRs for packets without a PCR.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pcr_interpolator_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_ts_pcr_interpolator *upipe_ts_pcr_interpolator = upipe_ts_pcr_interpolator_from_upipe(upipe);
    bool discontinuity = ubase_check(uref_flow_get_discontinuity(uref));
    if (discontinuity) {
        upipe_ts_pcr_interpolator->last_pcr = 0;
        upipe_ts_pcr_interpolator->packets = 0;
        upipe_ts_pcr_interpolator->pcr_packets = 0;
        upipe_ts_pcr_interpolator->pcr_delta = 0;
        upipe_ts_pcr_interpolator->discontinuity = true;
        upipe_notice_va(upipe, "Clearing state");
    }

    upipe_ts_pcr_interpolator->packets++;

    uint64_t pcr_prog = 0;
    uref_clock_get_cr_prog(uref, &pcr_prog);

    if (pcr_prog) {
        uint64_t delta = pcr_prog - upipe_ts_pcr_interpolator->last_pcr;
        upipe_ts_pcr_interpolator->last_pcr = pcr_prog;

        upipe_verbose_va(upipe,
                "pcr_prog %"PRId64" offset %"PRId64" stored offset %"PRIu64" bitrate %"PRId64" bps",
                pcr_prog, delta,
                upipe_ts_pcr_interpolator->pcr_delta,
                INT64_C(27000000) * upipe_ts_pcr_interpolator->packets * 188 * 8 / delta);

        if (upipe_ts_pcr_interpolator->pcr_delta)
            upipe_ts_pcr_interpolator->pcr_packets = upipe_ts_pcr_interpolator->packets;

        upipe_ts_pcr_interpolator->pcr_delta = delta;
        upipe_ts_pcr_interpolator->packets = 0;
    } else if (upipe_ts_pcr_interpolator->pcr_packets) {
        uint64_t offset = upipe_ts_pcr_interpolator->pcr_delta *
                    upipe_ts_pcr_interpolator->packets / upipe_ts_pcr_interpolator->pcr_packets;
        uint64_t prog = upipe_ts_pcr_interpolator->last_pcr + offset;
        uref_clock_set_date_prog(uref, prog, UREF_DATE_CR);
        upipe_throw_clock_ts(upipe, uref);
    }

    if (!upipe_ts_pcr_interpolator->pcr_packets) {
        uref_free(uref);
        return;
    }

    if (upipe_ts_pcr_interpolator->discontinuity) {
        uref_flow_set_discontinuity(uref);
        upipe_ts_pcr_interpolator->discontinuity = false;
    }
    upipe_ts_pcr_interpolator_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_pcr_interpolator_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_pcr_interpolator_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}


/** @internal @This processes control commands on a ts pcr interpolator pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_pcr_interpolator_control(struct upipe *upipe,
                                   int command, va_list args)
{
    struct upipe_ts_pcr_interpolator *upipe_ts_pcr_interpolator = upipe_ts_pcr_interpolator_from_upipe(upipe);

    UBASE_HANDLED_RETURN(
        upipe_ts_pcr_interpolator_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_pcr_interpolator_set_flow_def(upipe, flow_def);
        }
        case UPIPE_TS_PCR_INTERPOLATOR_GET_BITRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PCR_INTERPOLATOR_SIGNATURE)
            struct urational *urational = va_arg(args, struct urational *);
            urational->num = upipe_ts_pcr_interpolator->pcr_packets * 188 * 8;
            urational->den = upipe_ts_pcr_interpolator->pcr_delta;
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
static void upipe_ts_pcr_interpolator_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_pcr_interpolator *upipe_ts_pcr_interpolator = upipe_ts_pcr_interpolator_from_upipe(upipe);
    upipe_ts_pcr_interpolator_clean_output(upipe);
    upipe_ts_pcr_interpolator_clean_urefcount(upipe);
    upipe_ts_pcr_interpolator_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pcr_interpolator_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PCR_INTERPOLATOR_SIGNATURE,

    .upipe_alloc = upipe_ts_pcr_interpolator_alloc,
    .upipe_input = upipe_ts_pcr_interpolator_input,
    .upipe_control = upipe_ts_pcr_interpolator_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_pcr_interpolator pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pcr_interpolator_mgr_alloc(void)
{
    return &upipe_ts_pcr_interpolator_mgr;
}

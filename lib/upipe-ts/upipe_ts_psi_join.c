/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module joining tables of the PSI of a transport stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_psi_join.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** we only accept blocks containing PSI sections */
#define EXPECTED_FLOW_DEF "block.mpegtspsi."

/** @hidden */
static int upipe_ts_psi_join_build_flow_def(struct upipe *upipe);

/** @internal @This is the private context of a ts join pipe. */
struct upipe_ts_psi_join {
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

    /** list of input subpipes */
    struct uchain subs;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psi_join, upipe, UPIPE_TS_PSI_JOIN_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psi_join, urefcount, upipe_ts_psi_join_free)
UPIPE_HELPER_FLOW(upipe_ts_psi_join, EXPECTED_FLOW_DEF)
UPIPE_HELPER_OUTPUT(upipe_ts_psi_join, output, flow_def, output_state,
                    request_list)

/** @internal @This is the private context of an input of a ts_psi_join pipe. */
struct upipe_ts_psi_join_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** octetrate of the input */
    uint64_t octetrate;
    /** interval between PSI sections of the input */
    uint64_t section_interval;
    /** latency of the input */
    uint64_t latency;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psi_join_sub, upipe, UPIPE_TS_PSI_JOIN_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psi_join_sub, urefcount,
                       upipe_ts_psi_join_sub_free)
UPIPE_HELPER_VOID(upipe_ts_psi_join_sub)

UPIPE_HELPER_SUBPIPE(upipe_ts_psi_join, upipe_ts_psi_join_sub, sub, sub_mgr,
                     subs, uchain)

/** @internal @This allocates an input subpipe of a ts_psi_join pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psi_join_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct upipe *upipe = upipe_ts_psi_join_sub_alloc_void(mgr, uprobe,
            signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psi_join_sub *sub = upipe_ts_psi_join_sub_from_upipe(upipe);
    upipe_ts_psi_join_sub_init_urefcount(upipe);
    upipe_ts_psi_join_sub_init_sub(upipe);
    sub->octetrate = 0;
    sub->section_interval = 0;
    sub->latency = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_psi_join_sub_input(struct upipe *upipe, struct uref *uref,
                                        struct upump **upump_p)
{
    struct upipe_ts_psi_join *upipe_ts_psi_join =
        upipe_ts_psi_join_from_sub_mgr(upipe->mgr);

    upipe_ts_psi_join_output(upipe_ts_psi_join_to_upipe(upipe_ts_psi_join),
                             uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psi_join_sub_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_ts_psi_join *upipe_ts_psi_join =
        upipe_ts_psi_join_from_sub_mgr(upipe->mgr);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))

    struct upipe_ts_psi_join_sub *sub = upipe_ts_psi_join_sub_from_upipe(upipe);
    sub->octetrate = 0;
    uref_block_flow_get_octetrate(flow_def, &sub->octetrate);
    sub->section_interval = 0;
    uref_ts_flow_get_psi_section_interval(flow_def, &sub->section_interval);
    sub->latency = 0;
    uref_clock_get_latency(flow_def, &sub->latency);

    return upipe_ts_psi_join_build_flow_def(
            upipe_ts_psi_join_to_upipe(upipe_ts_psi_join));
}

/** @internal @This processes control commands on a subpipe of a ts_psi_join
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static int upipe_ts_psi_join_sub_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct upipe_ts_psi_join *upipe_ts_psi_join =
                upipe_ts_psi_join_from_sub_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psi_join_alloc_output_proxy(
                    upipe_ts_psi_join_to_upipe(upipe_ts_psi_join), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct upipe_ts_psi_join *upipe_ts_psi_join =
                upipe_ts_psi_join_from_sub_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psi_join_free_output_proxy(
                    upipe_ts_psi_join_to_upipe(upipe_ts_psi_join), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psi_join_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psi_join_sub_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees an input subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psi_join_sub_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_ts_psi_join_sub_clean_sub(upipe);
    upipe_ts_psi_join_sub_clean_urefcount(upipe);
    upipe_ts_psi_join_sub_free_void(upipe);
}

/** @internal @This initializes the input manager for a ts_psi_join pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psi_join_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_ts_psi_join *upipe_ts_psi_join =
        upipe_ts_psi_join_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_ts_psi_join->sub_mgr;
    sub_mgr->refcount = upipe_ts_psi_join_to_urefcount(upipe_ts_psi_join);
    sub_mgr->signature = UPIPE_TS_PSI_JOIN_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_ts_psi_join_sub_alloc;
    sub_mgr->upipe_input = upipe_ts_psi_join_sub_input;
    sub_mgr->upipe_control = upipe_ts_psi_join_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a ts_psi_join pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psi_join_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psi_join_alloc_flow(mgr, uprobe, signature,
                                                       args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_psi_join_init_urefcount(upipe);
    upipe_ts_psi_join_init_output(upipe);
    upipe_ts_psi_join_init_sub_mgr(upipe);
    upipe_ts_psi_join_init_sub_subs(upipe);

    upipe_throw_ready(upipe);
    upipe_ts_psi_join_store_flow_def(upipe, flow_def);

    return upipe;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_psi_join_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psi_join_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psi_join_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_psi_join_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_psi_join_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psi_join_iterate_sub(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_ts_psi_join_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_psi_join *upipe_ts_psi_join =
        upipe_ts_psi_join_from_upipe(upipe);

    struct uref *flow_def = upipe_ts_psi_join->flow_def;
    upipe_ts_psi_join->flow_def = NULL;

    uint64_t octetrate = 0;
    struct urational section_freq;
    section_freq.num = 0;
    section_freq.den = 1;
    uint64_t latency = 0;
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psi_join->subs, uchain) {
        struct upipe_ts_psi_join_sub *sub =
            upipe_ts_psi_join_sub_from_uchain(uchain);
        octetrate += sub->octetrate;
        if (sub->section_interval) {
            struct urational freq;
            freq.num = 1;
            freq.den = sub->section_interval;
            urational_simplify(&freq);
            section_freq = urational_add(&section_freq, &freq);
        }
        if (sub->latency > latency)
            latency = sub->latency;
    }

    if (octetrate) {
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def, octetrate))
    } else
        uref_block_flow_delete_octetrate(flow_def);
    if (section_freq.num && section_freq.den) {
        UBASE_FATAL(upipe, uref_ts_flow_set_psi_section_interval(flow_def,
                    section_freq.den / section_freq.num))
    } else
        uref_ts_flow_delete_psi_section_interval(flow_def);
    if (latency) {
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def, latency))
    } else
        uref_clock_delete_latency(flow_def);

    upipe_ts_psi_join_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psi_join_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_psi_join_clean_sub_subs(upipe);
    upipe_ts_psi_join_clean_output(upipe);
    upipe_ts_psi_join_clean_urefcount(upipe);
    upipe_ts_psi_join_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_psi_join_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PSI_JOIN_SIGNATURE,

    .upipe_alloc = upipe_ts_psi_join_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_ts_psi_join_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_psi_join pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psi_join_mgr_alloc(void)
{
    return &upipe_ts_psi_join_mgr;
}

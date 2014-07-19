/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module joining PIDs of a transport stream
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
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_join.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** we only accept blocks containing exactly one TS packet */
#define EXPECTED_FLOW_DEF "block.mpegts."
/** tolerance for the earliness of input packets */
#define CR_TOLERANCE (UCLOCK_FREQ / 1000)

/** @hidden */
static void upipe_ts_join_build_flow_def(struct upipe *upipe);

/** @internal @This is the private context of a ts join pipe. */
struct upipe_ts_join {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** max latency of the subpipes */
    uint64_t latency;

    /** list of input subpipes */
    struct uchain subs;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_join, upipe, UPIPE_TS_JOIN_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_join, urefcount, upipe_ts_join_free)
UPIPE_HELPER_VOID(upipe_ts_join)
UPIPE_HELPER_UREF_MGR(upipe_ts_join, uref_mgr)
UPIPE_HELPER_OUTPUT(upipe_ts_join, output, flow_def, flow_def_sent)

/** @internal @This is the private context of an input of a ts_join pipe. */
struct upipe_ts_join_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** input latency of the subpipe */
    uint64_t latency;
    /** true if the sub flow is subpicture */
    bool subpic;

    /** temporary uref storage */
    struct uchain urefs;
    /** number of urefs in storage */
    unsigned int nb_urefs;
    /** maximum number of urefs in storage */
    unsigned int max_urefs;
    /** next date that is supposed to be dequeued */
    uint64_t next_cr;
    /** last date that was dequeued */
    uint64_t last_cr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_join_sub, upipe, UPIPE_TS_JOIN_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_join_sub, urefcount, upipe_ts_join_sub_dead)
UPIPE_HELPER_VOID(upipe_ts_join_sub)

UPIPE_HELPER_SUBPIPE(upipe_ts_join, upipe_ts_join_sub, sub, sub_mgr,
                     subs, uchain)

/** @hidden */
static void upipe_ts_join_mux(struct upipe *upipe, struct upump **upump_p);

/** @internal @This allocates an input subpipe of a ts_join pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_join_sub_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_join_sub_alloc_void(mgr, uprobe, signature,
                                                       args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_join_sub *upipe_ts_join_sub =
        upipe_ts_join_sub_from_upipe(upipe);
    upipe_ts_join_sub_init_urefcount(upipe);
    upipe_ts_join_sub_init_sub(upipe);
    ulist_init(&upipe_ts_join_sub->urefs);
    upipe_ts_join_sub->nb_urefs = 0;
    upipe_ts_join_sub->max_urefs = 0;
    upipe_ts_join_sub->next_cr = upipe_ts_join_sub->last_cr = UINT64_MAX;
    upipe_ts_join_sub->latency = 0;
    upipe_ts_join_sub->subpic = false;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_join_sub_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_ts_join_sub *upipe_ts_join_sub =
        upipe_ts_join_sub_from_upipe(upipe);

    if (unlikely(upipe_ts_join_sub->max_urefs &&
                 upipe_ts_join_sub->nb_urefs >= upipe_ts_join_sub->max_urefs)) {
        upipe_dbg(upipe, "too many queued packets, dropping");
        uref_free(uref);
        return;
    }

    uint64_t cr;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &cr)))) {
        upipe_warn_va(upipe, "packet without date");
        uref_free(uref);
        return;
    }

    bool was_empty = ulist_empty(&upipe_ts_join_sub->urefs);
    ulist_add(&upipe_ts_join_sub->urefs, uref_to_uchain(uref));
    upipe_ts_join_sub->nb_urefs++;
    if (was_empty)
        upipe_ts_join_sub->next_cr = cr;

    struct upipe_ts_join *upipe_ts_join =
        upipe_ts_join_from_sub_mgr(upipe->mgr);
    upipe_ts_join_mux(upipe_ts_join_to_upipe(upipe_ts_join), upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_join_sub_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    struct upipe_ts_join_sub *upipe_ts_join_sub =
        upipe_ts_join_sub_from_upipe(upipe);
    upipe_ts_join_sub->subpic = !!strstr(def, "pic.sub.");
    uint64_t latency = 0;
    uref_clock_get_latency(flow_def, &latency);
    /* we never lower latency */
    if (latency > upipe_ts_join_sub->latency) {
        upipe_ts_join_sub->latency = latency;
        struct upipe_ts_join *upipe_ts_join =
            upipe_ts_join_from_sub_mgr(upipe->mgr);
        if (latency > upipe_ts_join->latency) {
            upipe_ts_join->latency = latency;
            upipe_ts_join_build_flow_def(upipe_ts_join_to_upipe(upipe_ts_join));
        }
    }
    return UBASE_ERR_NONE;
}

/** @internal @This gets the current max length of the internal queue.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the length
 * @return an error code
 */
static int upipe_ts_join_sub_get_max_length(struct upipe *upipe,
                                            unsigned int *p)
{
    struct upipe_ts_join_sub *upipe_ts_join_sub =
        upipe_ts_join_sub_from_upipe(upipe);
    assert(p != NULL);
    *p = upipe_ts_join_sub->max_urefs;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the max length of the internal queue.
 *
 * @param upipe description structure of the pipe
 * @param length new length
 * @return an error code
 */
static int upipe_ts_join_sub_set_max_length(struct upipe *upipe,
                                            unsigned int length)
{
    struct upipe_ts_join_sub *upipe_ts_join_sub =
        upipe_ts_join_sub_from_upipe(upipe);
    upipe_ts_join_sub->max_urefs = length;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a subpipe of a ts_join
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static int upipe_ts_join_sub_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_join_sub_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_join_sub_get_super(upipe, p);
        }
        case UPIPE_SINK_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_ts_join_sub_get_max_length(upipe, p);
        }
        case UPIPE_SINK_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_ts_join_sub_set_max_length(upipe, max_length);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This marks an input subpipe as dead.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_join_sub_dead(struct upipe *upipe)
{
    struct upipe_ts_join_sub *upipe_ts_join_sub =
        upipe_ts_join_sub_from_upipe(upipe);
    struct upipe_ts_join *upipe_ts_join =
        upipe_ts_join_from_sub_mgr(upipe->mgr);

    if (ulist_empty(&upipe_ts_join_sub->urefs)) {
        upipe_throw_dead(upipe);
        upipe_ts_join_sub_clean_sub(upipe);
        upipe_ts_join_sub_clean_urefcount(upipe);
        upipe_ts_join_sub_free_void(upipe);
    }
    upipe_ts_join_mux(upipe_ts_join_to_upipe(upipe_ts_join), NULL);
}

/** @internal @This initializes the input manager for a ts_join pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_join_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_ts_join *upipe_ts_join = upipe_ts_join_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_ts_join->sub_mgr;
    sub_mgr->refcount = upipe_ts_join_to_urefcount(upipe_ts_join);
    sub_mgr->signature = UPIPE_TS_JOIN_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_ts_join_sub_alloc;
    sub_mgr->upipe_input = upipe_ts_join_sub_input;
    sub_mgr->upipe_control = upipe_ts_join_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a ts_join pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_join_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_join_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_join_init_urefcount(upipe);
    upipe_ts_join_init_uref_mgr(upipe);
    upipe_ts_join_init_output(upipe);
    upipe_ts_join_init_sub_mgr(upipe);
    upipe_ts_join_init_sub_subs(upipe);
    struct upipe_ts_join *upipe_ts_join = upipe_ts_join_from_upipe(upipe);
    upipe_ts_join->latency = 0;

    upipe_throw_ready(upipe);

    if (ubase_check(upipe_ts_join_check_uref_mgr(upipe)))
        upipe_ts_join_build_flow_def(upipe);
    return upipe;
}

/** @internal @This finds the input with the lowest date.
 *
 * @param upipe description structure of the pipe
 * @return a pointer to the sub pipe, or NULL if not all inputs have packets
 */
static struct upipe_ts_join_sub *upipe_ts_join_find_input(struct upipe *upipe)
{
    struct upipe_ts_join *upipe_ts_join = upipe_ts_join_from_upipe(upipe);
    struct uchain *uchain;
    uint64_t earliest_cr = UINT64_MAX;
    struct upipe_ts_join_sub *earliest_input = NULL;
    ulist_foreach (&upipe_ts_join->subs, uchain) {
        struct upipe_ts_join_sub *input = upipe_ts_join_sub_from_uchain(uchain);
        if (input->next_cr == UINT64_MAX && !input->subpic)
            return NULL;
        if (input->next_cr < earliest_cr) {
            earliest_cr = input->next_cr;
            earliest_input = input;
        }
    }
    if (earliest_input == NULL)
        return NULL;

    return earliest_input;
}

/** @internal @This muxes TS packets to the output.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the last buffer
 */
static void upipe_ts_join_mux(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_join *upipe_ts_join = upipe_ts_join_from_upipe(upipe);
    struct upipe_ts_join_sub *input;
    while ((input = upipe_ts_join_find_input(upipe)) != NULL) {
        if (unlikely(upipe_ts_join->flow_def == NULL)) {
            if (unlikely(!ubase_check(upipe_ts_join_check_uref_mgr(upipe))))
                return;
        }

        if (unlikely(input->last_cr != UINT64_MAX &&
                     input->next_cr + CR_TOLERANCE < input->last_cr))
            upipe_warn_va(upipe_ts_join_sub_to_upipe(input),
                          "received a packet in the past (%"PRIu64" %"PRIu64")",
                          input->last_cr - input->next_cr, input->next_cr);
        input->last_cr = input->next_cr;

        struct uchain *uchain = ulist_pop(&input->urefs);
        input->nb_urefs--;
        struct uref *uref = uref_from_uchain(uchain);

        if (ulist_empty(&input->urefs)) {
            if (upipe_dead(upipe_ts_join_sub_to_upipe(input))) {
                upipe_throw_dead(upipe_ts_join_sub_to_upipe(input));
                upipe_ts_join_sub_clean_sub(upipe_ts_join_sub_to_upipe(input));
                upipe_ts_join_sub_clean_urefcount(upipe_ts_join_sub_to_upipe(input));
                upipe_ts_join_sub_free_void(upipe_ts_join_sub_to_upipe(input));
            } else {
                input->next_cr = UINT64_MAX;
            }
        } else {
            uchain = ulist_peek(&input->urefs);
            struct uref *next_uref = uref_from_uchain(uchain);
            uref_clock_get_cr_sys(next_uref, &input->next_cr);
        }

        upipe_ts_join_output(upipe, uref, upump_p);
    }
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_ts_join_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR:
            return upipe_ts_join_attach_uref_mgr(upipe);
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_join_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_join_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_join_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_join_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_join_iterate_sub(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_join_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_join *upipe_ts_join = upipe_ts_join_from_upipe(upipe);
    if (upipe_ts_join->uref_mgr == NULL)
        return;

    struct uref *flow_def =
        uref_block_flow_alloc_def(upipe_ts_join->uref_mgr, "mpegts.");
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (upipe_ts_join->latency)
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                                                  upipe_ts_join->latency))
    upipe_ts_join_store_flow_def(upipe, flow_def);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_join_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_ts_join_control(upipe, command, args))

    struct upipe_ts_join *upipe_ts_join = upipe_ts_join_from_upipe(upipe);
    if (upipe_ts_join->flow_def == NULL)
        upipe_ts_join_build_flow_def(upipe);

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_join_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_join_clean_sub_subs(upipe);
    upipe_ts_join_clean_output(upipe);
    upipe_ts_join_clean_uref_mgr(upipe);
    upipe_ts_join_clean_urefcount(upipe);
    upipe_ts_join_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_join_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_JOIN_SIGNATURE,

    .upipe_alloc = upipe_ts_join_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_ts_join_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_join pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_join_mgr_alloc(void)
{
    return &upipe_ts_join_mgr;
}

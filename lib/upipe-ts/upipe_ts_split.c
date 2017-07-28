/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module splitting PIDs of a transport stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_split.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks containing exactly one TS packet */
#define EXPECTED_FLOW_DEF "block.mpegts."
/** maximum number of PIDs */
#define MAX_PIDS 8192

/** @internal @This keeps internal information about a PID. */
struct upipe_ts_split_pid {
    /** subs specific to that PID */
    struct uchain subs;
    /** true if we asked for this PID */
    bool set;
};

/** @internal @This is the private context of a ts split pipe. */
struct upipe_ts_split {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** list of output subpipes */
    struct uchain subs;

    /** PIDs array */
    struct upipe_ts_split_pid pids[MAX_PIDS];

    /** manager to create output subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_split, upipe, UPIPE_TS_SPLIT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_split, urefcount, upipe_ts_split_no_input)
UPIPE_HELPER_VOID(upipe_ts_split)

UBASE_FROM_TO(upipe_ts_split, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ts_split_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of an output of a ts_split pipe. */
struct upipe_ts_split_sub {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists, all subs */
    struct uchain uchain;
    /** structure for double-linked lists, PID */
    struct uchain uchain_pid;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet on this output */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_split_sub, upipe, UPIPE_TS_SPLIT_OUTPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_split_sub, urefcount, upipe_ts_split_sub_free)
UPIPE_HELPER_FLOW(upipe_ts_split_sub, NULL)
UPIPE_HELPER_OUTPUT(upipe_ts_split_sub, output, flow_def, output_state, request_list)

UPIPE_HELPER_SUBPIPE(upipe_ts_split, upipe_ts_split_sub, sub, sub_mgr,
                     subs, uchain)

UBASE_FROM_TO(upipe_ts_split_sub, uchain, uchain_pid, uchain_pid)

/** @hidden */
static void upipe_ts_split_pid_set(struct upipe *upipe, uint16_t pid,
                                   struct upipe_ts_split_sub *output);
/** @hidden */
static void upipe_ts_split_pid_unset(struct upipe *upipe, uint16_t pid,
                                     struct upipe_ts_split_sub *output);

/** @internal @This allocates an output subpipe of a ts_split pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_split_sub_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_split_sub_alloc_flow(mgr, uprobe, signature,
                                                        args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_split_sub *upipe_ts_split_sub =
        upipe_ts_split_sub_from_upipe(upipe);
    upipe_ts_split_sub_init_urefcount(upipe);
    uchain_init(&upipe_ts_split_sub->uchain_pid);
    upipe_ts_split_sub_init_output(upipe);
    upipe_ts_split_sub_init_sub(upipe);
    upipe_ts_split_sub_store_flow_def(upipe, flow_def);

    struct upipe_ts_split *upipe_ts_split =
        upipe_ts_split_from_sub_mgr(upipe->mgr);
    uint64_t pid;
    if (likely(ubase_check(uref_ts_flow_get_pid(flow_def, &pid)) &&
               pid < MAX_PIDS))
        upipe_ts_split_pid_set(upipe_ts_split_to_upipe(upipe_ts_split), pid,
                               upipe_ts_split_sub);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes control commands on an output subpipe of a
 * ts_split pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_split_sub_control(struct upipe *upipe,
                                      int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_ts_split_sub_control_output(upipe, command, args);
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_split_sub_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_split_sub_free(struct upipe *upipe)
{
    struct upipe_ts_split_sub *upipe_ts_split_sub =
        upipe_ts_split_sub_from_upipe(upipe);
    struct upipe_ts_split *upipe_ts_split =
        upipe_ts_split_from_sub_mgr(upipe->mgr);

    /* remove output from the subs list */
    if (upipe_ts_split_sub->flow_def != NULL) {
        uint64_t pid;
        if (ubase_check(uref_ts_flow_get_pid(upipe_ts_split_sub->flow_def, &pid)))
            upipe_ts_split_pid_unset(
                    upipe_ts_split_to_upipe(upipe_ts_split), pid,
                    upipe_ts_split_sub);
    }

    upipe_throw_dead(upipe);
    upipe_ts_split_sub_clean_output(upipe);
    upipe_ts_split_sub_clean_sub(upipe);
    upipe_ts_split_sub_clean_urefcount(upipe);
    upipe_ts_split_sub_free_flow(upipe);
}

/** @internal @This initializes the output manager for a ts_split pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_split_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_ts_split->sub_mgr;
    sub_mgr->refcount = upipe_ts_split_to_urefcount_real(upipe_ts_split);
    sub_mgr->signature = UPIPE_TS_SPLIT_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_ts_split_sub_alloc;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_ts_split_sub_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a ts_split pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_split_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_split_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    upipe_ts_split_init_urefcount(upipe);
    urefcount_init(upipe_ts_split_to_urefcount_real(upipe_ts_split),
                   upipe_ts_split_free);
    upipe_ts_split_init_sub_mgr(upipe);
    upipe_ts_split_init_sub_subs(upipe);

    int i;
    for (i = 0; i < MAX_PIDS; i++) {
        ulist_init(&upipe_ts_split->pids[i].subs);
        upipe_ts_split->pids[i].set = false;
    }
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks the status of the PID, and sends the split_set_pid
 * or split_unset_pid event if it has not already been sent.
 *
 * @param upipe description structure of the pipe
 * @param pid PID to check
 */
static void upipe_ts_split_pid_check(struct upipe *upipe, uint16_t pid)
{
    assert(pid < MAX_PIDS);
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    if (!ulist_empty(&upipe_ts_split->pids[pid].subs)) {
        if (!upipe_ts_split->pids[pid].set) {
            upipe_ts_split->pids[pid].set = true;
            upipe_dbg_va(upipe, "throw ts split add pid %"PRIu16, pid);
            upipe_throw(upipe, UPROBE_TS_SPLIT_ADD_PID,
                        UPIPE_TS_SPLIT_SIGNATURE, (unsigned int)pid);
        }
    } else {
        if (upipe_ts_split->pids[pid].set) {
            upipe_ts_split->pids[pid].set = false;
            upipe_dbg_va(upipe, "throw ts split del pid %"PRIu16, pid);
            upipe_throw(upipe, UPROBE_TS_SPLIT_DEL_PID,
                        UPIPE_TS_SPLIT_SIGNATURE, (unsigned int)pid);
        }
    }
}

/** @internal @This adds an output to a given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @param output output sub-structure
 */
static void upipe_ts_split_pid_set(struct upipe *upipe, uint16_t pid,
                                   struct upipe_ts_split_sub *output)
{
    assert(pid < MAX_PIDS);
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    ulist_add(&upipe_ts_split->pids[pid].subs,
              upipe_ts_split_sub_to_uchain_pid(output));
    upipe_ts_split_pid_check(upipe, pid);
}

/** @internal @This removes an output from a given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @param output output sub-structure
 */
static void upipe_ts_split_pid_unset(struct upipe *upipe, uint16_t pid,
                                     struct upipe_ts_split_sub *output)
{
    assert(pid < MAX_PIDS);
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_split->pids[pid].subs, uchain, uchain_tmp) {
        if (output == upipe_ts_split_sub_from_uchain_pid(uchain)) {
            ulist_delete(uchain);
        }
    }
    upipe_ts_split_pid_check(upipe, pid);
}

/** @internal @This demuxes a TS packet to the appropriate output(s).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_split_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    uint8_t buffer[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, TS_HEADER_SIZE,
                                               buffer);
    if (unlikely(ts_header == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uint16_t pid = ts_get_pid(ts_header);
    UBASE_FATAL(upipe, uref_block_peek_unmap(uref, 0, buffer, ts_header))

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_split->pids[pid].subs, uchain) {
        struct upipe_ts_split_sub *output =
                upipe_ts_split_sub_from_uchain_pid(uchain);
        if (likely(uchain->next == NULL)) {
            upipe_ts_split_sub_output(upipe_ts_split_sub_to_upipe(output),
                                      uref, upump_p);
            uref = NULL;
        } else {
            struct uref *new_uref = uref_dup(uref);
            if (likely(new_uref != NULL))
                upipe_ts_split_sub_output(
                        upipe_ts_split_sub_to_upipe(output),
                        new_uref, upump_p);
            else {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }
        }
    }
    if (uref != NULL)
        uref_free(uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_split_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    return uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF);
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_split_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_split_control_subs(upipe, command, args));

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            /* We do not pass through the requests ; which output would
             * we use ? */
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_split_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_split_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_split *upipe_ts_split =
        upipe_ts_split_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_split_to_upipe(upipe_ts_split);
    upipe_throw_dead(upipe);
    upipe_ts_split_clean_sub_subs(upipe);
    urefcount_clean(urefcount_real);
    upipe_ts_split_clean_urefcount(upipe);
    upipe_ts_split_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_split_no_input(struct upipe *upipe)
{
    struct upipe_ts_split *upipe_ts_split =
        upipe_ts_split_from_upipe(upipe);
    upipe_ts_split_throw_sub_subs(upipe, UPROBE_SOURCE_END);
    urefcount_release(upipe_ts_split_to_urefcount_real(upipe_ts_split));
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_split_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SPLIT_SIGNATURE,

    .upipe_alloc = upipe_ts_split_alloc,
    .upipe_input = upipe_ts_split_input,
    .upipe_control = upipe_ts_split_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_split_mgr_alloc(void)
{
    return &upipe_ts_split_mgr;
}

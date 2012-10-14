/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe module splitting PIDS of a transport stream
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_split_outputs.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_split.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks containing exactly one TS packet */
#define EXPECTED_FLOW_DEF "block.mpegts"
/** maximum number of PIDs */
#define MAX_PIDS 8192

/** @internal @This keeps internal information about a PID. */
struct upipe_ts_split_pid {
    /** outputs specific to that PID */
    struct ulist outputs;
    /** last continuity counter, or -1 */
    int8_t last_cc;
    /** true if we asked for this PID */
    bool set;
};

/** @internal @This is the private context of a ts_split pipe. */
struct upipe_ts_split {
    /** list of outputs */
    struct ulist outputs;
    /** input flow name */
    char *flow_name;

    /** PIDs array */
    struct upipe_ts_split_pid pids[MAX_PIDS];
    /** true if we have thrown the ready event */
    bool ready;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_split, upipe)

/** @internal @This is the private context of an output of a ts_split pipe. */
struct upipe_ts_split_output {
    /** structure for double-linked lists (in outputs ulist) */
    struct uchain uchain;
    /** structure for double-linked lists (in pid ulist) */
    struct uchain pid_uchain;
    /** suffix added to every flow on this output */
    char *flow_suffix;
    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet on this output */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
};

/** @internal @This returns the uchain for PID chaining.
 *
 * @param block pointer to the upipe_ts_split_output structure
 * @return pointer to uchain
 */
static inline struct uchain *upipe_ts_split_output_to_pid_uchain(struct upipe_ts_split_output *output)
{
    return &output->pid_uchain;
}

/** @hidden */
static void upipe_ts_split_pid_unset(struct upipe *upipe, uint16_t pid,
                                     struct upipe_ts_split_output *output);

/** @internal @This returns the upipe_ts_split_output structure.
 *
 * @param uchain pointer to uchain
 * @return pointer to the upipe_ts_split_output structure
 */
static inline struct upipe_ts_split_output *upipe_ts_split_output_from_pid_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upipe_ts_split_output, pid_uchain);
}

UPIPE_HELPER_SPLIT_OUTPUT(upipe_ts_split, upipe_ts_split_output, uchain, output,
                          flow_suffix, flow_def, flow_def_sent)
UPIPE_HELPER_SPLIT_OUTPUTS(upipe_ts_split, outputs, upipe_ts_split_output)
UPIPE_HELPER_SPLIT_FLOW_NAME(upipe_ts_split, outputs, flow_name,
                             upipe_ts_split_output)

/** @internal @This allocates and initializes a new output-specific
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param flow_suffix flow suffix
 * @return pointer to allocated substructure
 */
static struct upipe_ts_split_output *
    upipe_ts_split_output_alloc(struct upipe *upipe, const char *flow_suffix)
{
    assert(flow_suffix != NULL);
    struct upipe_ts_split_output *output =
        malloc(sizeof(struct upipe_ts_split_output));
    if (unlikely(output == NULL))
        return NULL;
    if (unlikely(!upipe_ts_split_output_init(upipe, output, flow_suffix))) {
        free(output);
        return NULL;
    }
    return output;
}

/** @internal @This frees an output-specific substructure.
 *
 * @param upipe description structure of the pipe
 * @param output substructure to free
 */
static void upipe_ts_split_output_free(struct upipe *upipe,
                                       struct upipe_ts_split_output *output)
{
    uint64_t pid = MAX_PIDS;
    if (likely(output->flow_def != NULL))
        uref_ts_flow_get_pid(output->flow_def, &pid);
    upipe_ts_split_output_clean(upipe, output);
    if (likely(pid < MAX_PIDS))
        upipe_ts_split_pid_unset(upipe, pid, output);
    free(output);
}

/** @internal @This allocates a ts_split pipe.
 *
 * @param mgr common management structure
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_split_alloc(struct upipe_mgr *mgr)
{
    struct upipe_ts_split *upipe_ts_split =
        malloc(sizeof(struct upipe_ts_split));
    if (unlikely(upipe_ts_split == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_split_to_upipe(upipe_ts_split);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_TS_SPLIT_SIGNATURE;
    urefcount_init(&upipe_ts_split->refcount);
    upipe_ts_split_init_outputs(upipe);
    upipe_ts_split_init_flow_name(upipe);

    for (int i = 0; i < MAX_PIDS; i++) {
        ulist_init(&upipe_ts_split->pids[i].outputs);
        upipe_ts_split->pids[i].last_cc = -1;
        upipe_ts_split->pids[i].set = false;
    }
    upipe_ts_split->ready = false;
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
    if (!ulist_empty(&upipe_ts_split->pids[pid].outputs)) {
        if (!upipe_ts_split->pids[pid].set) {
            upipe_ts_split->pids[pid].set = true;
            upipe_throw(upipe, UPROBE_TS_SPLIT_SET_PID,
                        UPIPE_TS_SPLIT_SIGNATURE, (unsigned int)pid);
        }
    } else {
        if (upipe_ts_split->pids[pid].set) {
            upipe_ts_split->pids[pid].set = false;
            upipe_throw(upipe, UPROBE_TS_SPLIT_UNSET_PID,
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
                                   struct upipe_ts_split_output *output)
{
    assert(pid < MAX_PIDS);
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    ulist_add(&upipe_ts_split->pids[pid].outputs,
              upipe_ts_split_output_to_pid_uchain(output));
    upipe_ts_split_pid_check(upipe, pid);
}

/** @internal @This removes an output from a given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @param output output sub-structure
 */
static void upipe_ts_split_pid_unset(struct upipe *upipe, uint16_t pid,
                                     struct upipe_ts_split_output *output)
{
    assert(pid < MAX_PIDS);
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    struct uchain *uchain;
    ulist_delete_foreach (&upipe_ts_split->pids[pid].outputs, uchain) {
        if (output == upipe_ts_split_output_from_pid_uchain(uchain))
            ulist_delete(&upipe_ts_split->pids[pid].outputs, uchain);
    }
    upipe_ts_split_pid_check(upipe, pid);
}

/** @internal @This demuxes a TS packet to the appropriate output.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_split_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    uint8_t buffer[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, TS_HEADER_SIZE,
                                               buffer);
    if (unlikely(ts_header == NULL)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        uref_free(uref);
        return;
    }
    uint16_t pid = ts_get_pid(ts_header);
    bool ret = uref_block_peek_unmap(uref, 0, TS_HEADER_SIZE,
                                     buffer, ts_header);
    assert(ret);

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_split->pids[pid].outputs, uchain) {
        struct upipe_ts_split_output *output =
                upipe_ts_split_output_from_pid_uchain(uchain);
        struct uref *new_uref = uref_dup(uref);
        if (likely(new_uref != NULL))
            upipe_ts_split_output_output(upipe, output, new_uref);
        else {
            uref_free(uref);
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return;
        }
    }
    uref_free(uref);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_ts_split_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);

    const char *flow, *def;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_free(uref);
       return false;
    }

    if (unlikely(uref_flow_get_delete(uref))) {
        upipe_ts_split_set_flow_name(upipe, NULL);
        uref_free(uref);
        return true;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (upipe_ts_split->flow_name != NULL) {
            ulog_warning(upipe->ulog,
                         "received flow definition without delete first");
            upipe_ts_split_set_flow_name(upipe, NULL);
        }
        if (unlikely(strncmp(def, EXPECTED_FLOW_DEF,
                             strlen(EXPECTED_FLOW_DEF)))) {
            ulog_warning(upipe->ulog,
                         "received an incompatible flow definition");
            uref_free(uref);
            return false;
        }

        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        upipe_ts_split_set_flow_name(upipe, flow);
        uref_free(uref);
        return true;
    }

    if (unlikely(upipe_ts_split->flow_name == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(strcmp(upipe_ts_split->flow_name, flow))) {
        ulog_warning(upipe->ulog,
                     "received a buffer not matching the current flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return true;
    }

    upipe_ts_split_work(upipe, uref);
    return true;
}

/** @internal @This gets the flow definition on an output. The uref returned
 * may not be modified nor freed.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the flow definition packet
 * @param flow_suffix flow suffix
 * @return false in case of error
 */
static bool upipe_ts_split_get_flow_def(struct upipe *upipe,
                                        struct uref **p,
                                        const char *flow_suffix)
{
    assert(p != NULL);
    assert(flow_suffix != NULL);

    struct upipe_ts_split_output *output =
        upipe_ts_split_find_output(upipe, flow_suffix);
    if (unlikely(output == NULL))
        return false;
    *p = output->flow_def;
    return true;
}

/** @internal @This sets the flow definition on an output. It must be called
 * before @ref upipe_ts_split_set_output, because it allows to create
 * non-existant outputs. If flow_def is NULL, the output is deleted.
 *
 * The attribute t.pid must be set on the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @param flow_suffix flow suffix
 * @return false in case of error
 */
static bool upipe_ts_split_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def,
                                        const char *flow_suffix)
{
    assert(flow_suffix != NULL);
    if (flow_def == NULL) {
        ulog_debug(upipe->ulog, "deleting output: %s", flow_suffix);
        return upipe_ts_split_delete_output(upipe, flow_suffix,
                                            upipe_ts_split_output_free);

    } else {
        uint64_t pid;
        if (unlikely(!uref_ts_flow_get_pid(flow_def, &pid) || pid >= MAX_PIDS))
            return false;

        struct upipe_ts_split_output *output =
            upipe_ts_split_find_output(upipe, flow_suffix);

        if (output == NULL) {
            ulog_debug(upipe->ulog, "adding output: %s", flow_suffix);
            output = upipe_ts_split_output_alloc(upipe, flow_suffix);
            if (unlikely(output == NULL)) {
                ulog_aerror(upipe->ulog);
                upipe_throw_aerror(upipe);
                return false;
            }
            upipe_ts_split_add_output(upipe, output);
        } else {
            uint64_t old_pid;
            if (unlikely(!uref_ts_flow_get_pid(output->flow_def, &old_pid)))
                return false;
            upipe_ts_split_pid_unset(upipe, old_pid, output);
        }
        upipe_ts_split_output_set_flow_def(upipe, output, flow_def);
        upipe_ts_split_pid_set(upipe, pid, output);
    }

    return true;
}

/** @internal @This processes control commands on a ts split pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_ts_split_control(struct upipe *upipe,
                                    enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_SPLIT_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_split_get_output(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_split_set_output(upipe, output, flow_suffix);
        }
        case UPIPE_SPLIT_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_split_get_flow_def(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_split_set_flow_def(upipe, flow_def, flow_suffix);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a ts sync pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_split_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_ts_split_input(upipe, uref);
    }

    if (unlikely(!_upipe_ts_split_control(upipe, command, args)))
        return false;

    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    if (likely(!upipe_ts_split->ready)) {
        upipe_ts_split->ready = true;
        upipe_throw_ready(upipe);
    }

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_split_use(struct upipe *upipe)
{
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    urefcount_use(&upipe_ts_split->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_split_release(struct upipe *upipe)
{
    struct upipe_ts_split *upipe_ts_split = upipe_ts_split_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_split->refcount))) {
        upipe_ts_split_clean_flow_name(upipe);
        upipe_ts_split_clean_outputs(upipe, upipe_ts_split_output_free);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_split->refcount);
        free(upipe_ts_split);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_split_mgr = {
    .upipe_alloc = upipe_ts_split_alloc,
    .upipe_control = upipe_ts_split_control,
    .upipe_use = upipe_ts_split_use,
    .upipe_release = upipe_ts_split_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_splits
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_split_mgr_alloc(void)
{
    return &upipe_ts_split_mgr;
}

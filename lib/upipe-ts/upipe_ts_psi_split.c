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
 * @short Upipe module splitting tables of the PSI of a transport stream
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
#include <upipe-ts/upipe_ts_psi_split.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks containing exactly one PSI section */
#define EXPECTED_FLOW_DEF "block.mpegtspsi."

/** @internal @This keeps internal information about a PID. */
struct upipe_ts_psi_split_pid {
    /** outputs specific to that PID */
    struct ulist outputs;
    /** true if we asked for this PID */
    bool set;
};

/** @internal @This is the private context of a ts_psi_split pipe. */
struct upipe_ts_psi_split {
    /** list of outputs */
    struct ulist outputs;
    /** input flow name */
    char *flow_name;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psi_split, upipe)

/** @internal @This is the private context of an output of a ts_psi_split pipe. */
struct upipe_ts_psi_split_output {
    /** structure for double-linked lists (in outputs ulist) */
    struct uchain uchain;
    /** suffix added to every flow on this output */
    char *flow_suffix;
    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet on this output */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
};

UPIPE_HELPER_SPLIT_OUTPUT(upipe_ts_psi_split, upipe_ts_psi_split_output, uchain,
                          output, flow_suffix, flow_def, flow_def_sent)
UPIPE_HELPER_SPLIT_OUTPUTS(upipe_ts_psi_split, outputs,
                           upipe_ts_psi_split_output)
UPIPE_HELPER_SPLIT_FLOW_NAME(upipe_ts_psi_split, outputs, flow_name,
                             upipe_ts_psi_split_output)

/** @internal @This allocates and initializes a new output-specific
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param flow_suffix flow suffix
 * @return pointer to allocated substructure
 */
static struct upipe_ts_psi_split_output *
    upipe_ts_psi_split_output_alloc(struct upipe *upipe,
                                    const char *flow_suffix)
{
    assert(flow_suffix != NULL);
    struct upipe_ts_psi_split_output *output =
        malloc(sizeof(struct upipe_ts_psi_split_output));
    if (unlikely(output == NULL))
        return NULL;
    if (unlikely(!upipe_ts_psi_split_output_init(upipe, output, flow_suffix))) {
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
static void
    upipe_ts_psi_split_output_free(struct upipe *upipe,
                                   struct upipe_ts_psi_split_output *output)
{
    upipe_ts_psi_split_output_clean(upipe, output);
    free(output);
}

/** @internal @This allocates a ts_psi_split pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psi_split_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              struct ulog *ulog)
{
    struct upipe_ts_psi_split *upipe_ts_psi_split =
        malloc(sizeof(struct upipe_ts_psi_split));
    if (unlikely(upipe_ts_psi_split == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_psi_split_to_upipe(upipe_ts_psi_split);
    upipe_init(upipe, uprobe, ulog);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_TS_PSI_SPLIT_SIGNATURE;
    urefcount_init(&upipe_ts_psi_split->refcount);
    upipe_ts_psi_split_init_outputs(upipe);
    upipe_ts_psi_split_init_flow_name(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This demuxes a PSI section to the appropriate output(s).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_psi_split_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_psi_split *upipe_ts_psi_split =
        upipe_ts_psi_split_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psi_split->outputs, uchain) {
        struct upipe_ts_psi_split_output *output =
                upipe_ts_psi_split_output_from_uchain(uchain);
        const uint8_t *filter, *mask;
        size_t size;
        if (uref_ts_flow_get_psi_filter(output->flow_def, &filter, &mask,
                                        &size) &&
            uref_block_match(uref, filter, mask, size)) {
            struct uref *new_uref = uref_dup(uref);
            if (likely(new_uref != NULL))
                upipe_ts_psi_split_output_output(upipe, output, new_uref);
            else {
                uref_free(uref);
                ulog_aerror(upipe->ulog);
                upipe_throw_aerror(upipe);
                return;
            }
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
static bool upipe_ts_psi_split_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_psi_split *upipe_ts_psi_split =
        upipe_ts_psi_split_from_upipe(upipe);

    const char *flow, *def;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_free(uref);
       return false;
    }

    if (unlikely(uref_flow_get_delete(uref))) {
        upipe_ts_psi_split_set_flow_name(upipe, NULL);
        uref_free(uref);
        return true;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(upipe_ts_psi_split->flow_name != NULL)) {
            ulog_warning(upipe->ulog,
                         "received flow definition without delete first");
            upipe_ts_psi_split_set_flow_name(upipe, NULL);
        }
        if (unlikely(strncmp(def, EXPECTED_FLOW_DEF,
                             strlen(EXPECTED_FLOW_DEF)))) {
            ulog_warning(upipe->ulog,
                         "received an incompatible flow definition");
            uref_free(uref);
            return false;
        }

        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        upipe_ts_psi_split_set_flow_name(upipe, flow);
        uref_free(uref);
        return true;
    }

    if (unlikely(upipe_ts_psi_split->flow_name == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(strcmp(upipe_ts_psi_split->flow_name, flow))) {
        ulog_warning(upipe->ulog,
                     "received a buffer not matching the current flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return true;
    }

    upipe_ts_psi_split_work(upipe, uref);
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
static bool upipe_ts_psi_split_get_flow_def(struct upipe *upipe,
                                            struct uref **p,
                                            const char *flow_suffix)
{
    assert(p != NULL);
    assert(flow_suffix != NULL);

    struct upipe_ts_psi_split_output *output =
        upipe_ts_psi_split_find_output(upipe, flow_suffix);
    if (unlikely(output == NULL))
        return false;
    *p = output->flow_def;
    return true;
}

/** @internal @This sets the flow definition on an output. It must be called
 * before @ref upipe_ts_psi_split_set_output, because it allows to create
 * non-existant outputs. If flow_def is NULL, the output is deleted.
 *
 * The attribute t.psi.filter should be set on the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @param flow_suffix flow suffix
 * @return false in case of error
 */
static bool upipe_ts_psi_split_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def,
                                        const char *flow_suffix)
{
    assert(flow_suffix != NULL);
    if (flow_def == NULL) {
        ulog_debug(upipe->ulog, "deleting output: %s", flow_suffix);
        return upipe_ts_psi_split_delete_output(upipe, flow_suffix,
                                                upipe_ts_psi_split_output_free);

    } else {
        struct upipe_ts_psi_split_output *output =
            upipe_ts_psi_split_find_output(upipe, flow_suffix);

        if (output == NULL) {
            ulog_debug(upipe->ulog, "adding output: %s", flow_suffix);
            output = upipe_ts_psi_split_output_alloc(upipe, flow_suffix);
            if (unlikely(output == NULL)) {
                ulog_aerror(upipe->ulog);
                upipe_throw_aerror(upipe);
                return false;
            }
            upipe_ts_psi_split_add_output(upipe, output);
        }
        upipe_ts_psi_split_output_set_flow_def(upipe, output, flow_def);
    }

    return true;
}

/** @internal @This processes control commands on a ts_psi_split pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_ts_psi_split_control(struct upipe *upipe,
                                        enum upipe_command command,
                                        va_list args)
{
    switch (command) {
        case UPIPE_SPLIT_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_psi_split_get_output(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_psi_split_set_output(upipe, output, flow_suffix);
        }
        case UPIPE_SPLIT_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_psi_split_get_flow_def(upipe, p, flow_suffix);
        }
        case UPIPE_SPLIT_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *flow_suffix = va_arg(args, const char *);
            return upipe_ts_psi_split_set_flow_def(upipe, flow_def, flow_suffix);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a ts_psi_split pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psi_split_control(struct upipe *upipe,
                                       enum upipe_command command, va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_ts_psi_split_input(upipe, uref);
    }

    return _upipe_ts_psi_split_control(upipe, command, args);
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psi_split_use(struct upipe *upipe)
{
    struct upipe_ts_psi_split *upipe_ts_psi_split = upipe_ts_psi_split_from_upipe(upipe);
    urefcount_use(&upipe_ts_psi_split->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psi_split_release(struct upipe *upipe)
{
    struct upipe_ts_psi_split *upipe_ts_psi_split = upipe_ts_psi_split_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_psi_split->refcount))) {
        upipe_ts_psi_split_clean_flow_name(upipe);
        upipe_ts_psi_split_clean_outputs(upipe, upipe_ts_psi_split_output_free);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_psi_split->refcount);
        free(upipe_ts_psi_split);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_psi_split_mgr = {
    .upipe_alloc = upipe_ts_psi_split_alloc,
    .upipe_control = upipe_ts_psi_split_control,
    .upipe_use = upipe_ts_psi_split_use,
    .upipe_release = upipe_ts_psi_split_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_psi_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psi_split_mgr_alloc(void)
{
    return &upipe_ts_psi_split_mgr;
}

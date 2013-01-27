/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe higher-level module demuxing elementary streams of a TS
 * Four parts in this file:
 * @list
 * @item psi_pid structure, which handles PSI demultiplexing from ts_split
 * until ts_psi_split
 * @item output source pipe, which is returned to the application, and
 * represents an elementary stream; it sets up the ts_decaps, pes_decaps and
 * framer subpipes
 * @item program split pipe, which is returned to the application, and
 * represents a program; it sets up the ts_split_output and ts_pmtd subpipes
 * @item demux sink pipe which sets up the ts_split, ts_patd and optional input
 * synchronizer subpipes
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/ulog_sub.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-ts/upipe_ts_sync.h>
#include <upipe-ts/upipe_ts_check.h>
#include <upipe-ts/upipe_ts_decaps.h>
#include <upipe-ts/upipe_ts_psim.h>
#include <upipe-ts/upipe_ts_psi_split.h>
#include <upipe-ts/upipe_ts_patd.h>
#include <upipe-ts/upipe_ts_pmtd.h>
#include <upipe-ts/upipe_ts_pesd.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>

/** we only accept all kinds of blocks */
#define EXPECTED_FLOW_DEF "block."
/** but already sync'ed TS packets are better */
#define EXPECTED_FLOW_DEF_SYNC "block.mpegts."
/** or otherwise aligned TS packets to check */
#define EXPECTED_FLOW_DEF_CHECK "block.mpegtsaligned."
/** maximum number of PIDs */
#define MAX_PIDS 8192

/** @internal @This is the private context of a ts_demux manager. */
struct upipe_ts_demux_mgr {
    /** pointer to ts_split manager */
    struct upipe_mgr *ts_split_mgr;

    /* inputs */
    /** pointer to ts_sync manager */
    struct upipe_mgr *ts_sync_mgr;
    /** pointer to ts_check manager */
    struct upipe_mgr *ts_check_mgr;

    /** pointer to ts_decaps manager */
    struct upipe_mgr *ts_decaps_mgr;

    /* PSI */
    /** pointer to ts_psim manager */
    struct upipe_mgr *ts_psim_mgr;
    /** pointer to ts_psi_split manager */
    struct upipe_mgr *ts_psi_split_mgr;
    /** pointer to ts_patd manager */
    struct upipe_mgr *ts_patd_mgr;
    /** pointer to ts_pmtd manager */
    struct upipe_mgr *ts_pmtd_mgr;

    /* ES */
    /** pointer to ts_pesd manager */
    struct upipe_mgr *ts_pesd_mgr;
    /** pointer to mp2vf manager */
    struct upipe_mgr *mp2vf_mgr;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

/** @internal @This returns the high-level upipe_mgr structure.
 *
 * @param ts_demux_mgr pointer to the upipe_ts_demux_mgr structure
 * @return pointer to the upipe_mgr structure
 */
static inline struct upipe_mgr *
    upipe_ts_demux_mgr_to_upipe_mgr(struct upipe_ts_demux_mgr *ts_demux_mgr)
{
    return &ts_demux_mgr->mgr;
}

/** @internal @This returns the private upipe_ts_demux_mgr structure.
 *
 * @param mgr description structure of the upipe manager
 * @return pointer to the upipe_ts_demux_mgr structure
 */
static inline struct upipe_ts_demux_mgr *
    upipe_ts_demux_mgr_from_upipe_mgr(struct upipe_mgr *mgr)
{
    return container_of(mgr, struct upipe_ts_demux_mgr, mgr);
}

/** @internal @This is the input mode of a ts_demux pipe. */
enum upipe_ts_demux_mode {
    /** no input configured */
    UPIPE_TS_DEMUX_OFF,
    /** already synchronized packets */
    UPIPE_TS_DEMUX_SYNC,
    /** already aligned packets */
    UPIPE_TS_DEMUX_CHECK,
    /** non-synchronized, unaligned packets */
    UPIPE_TS_DEMUX_SCAN
};

/** @hidden */
struct upipe_ts_demux_psi_pid;

/** @internal @This is the private context of a ts_demux pipe. */
struct upipe_ts_demux {
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** true if we received a compatible flow definition */
    bool flow_def_ok;
    /** input mode */
    enum upipe_ts_demux_mode input_mode;
    /** pointer to input subpipe */
    struct upipe *input;

    /** pointer to ts_split subpipe */
    struct upipe *split;
    /** psi_pid structure for PAT */
    struct upipe_ts_demux_psi_pid *psi_pid_pat;
    /** ts_psi_split_output subpipe for PAT */
    struct upipe *psi_split_output_pat;

    /** list of PIDs carrying PSI */
    struct ulist psi_pids;
    /** PID of the NIT */
    uint16_t nit_pid;
    /** true if the conformance is guessed from the stream */
    bool auto_conformance;
    /** current conformance */
    enum upipe_ts_demux_conformance conformance;

    /** probe to get new flow events from subpipes created by psi_pid objects */
    struct uprobe psi_pid_plumber;
    /** probe to get new flow events from ts_psim subpipes created by psi_pid
     * objects */
    struct uprobe psim_plumber;
    /** probe to get events from ts_patd subpipe */
    struct uprobe patd_probe;

    /** list of programs */
    struct ulist programs;

    /** manager to create programs */
    struct upipe_mgr output_mgr;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux, upipe)
UPIPE_HELPER_UREF_MGR(upipe_ts_demux, uref_mgr)

/** @internal @This returns the public output_mgr structure.
 *
 * @param upipe_ts_demux pointer to the private upipe_ts_demux structure
 * @return pointer to the public output_mgr structure
 */
static inline struct upipe_mgr *
    upipe_ts_demux_to_output_mgr(struct upipe_ts_demux *s)
{
    return &s->output_mgr;
}

/** @internal @This returns the private upipe_ts_demux structure.
 *
 * @param output_mgr public output_mgr structure of the pipe
 * @return pointer to the private upipe_ts_demux structure
 */
static inline struct upipe_ts_demux *
    upipe_ts_demux_from_output_mgr(struct upipe_mgr *output_mgr)
{
    return container_of(output_mgr, struct upipe_ts_demux, output_mgr);
}

/** @internal @This is the private context of a program of a ts_demux pipe. */
struct upipe_ts_demux_program {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** flow definition set */
    struct uref *flow_def;
    /** program number */
    unsigned int program;
    /** PCR PID */
    uint16_t pcr_pid;
    /** ts_psi_split_output subpipe */
    struct upipe *psi_split_output;
    /** pointer to psi_pid structure */
    struct upipe_ts_demux_psi_pid *psi_pid;

    /** probe to get events from subpipes */
    struct uprobe plumber;
    /** probe to get events from ts_pmtd subpipe */
    struct uprobe pmtd_probe;

    /** list of outputs */
    struct ulist outputs;

    /** manager to create outputs */
    struct upipe_mgr output_mgr;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux_program, upipe)

/** @internal @This returns the public output_mgr structure.
 *
 * @param upipe_ts_demux_program pointer to the private upipe_ts_demux_program
 * structure
 * @return pointer to the public output_mgr structure
 */
static inline struct upipe_mgr *
    upipe_ts_demux_program_to_output_mgr(struct upipe_ts_demux_program *s)
{
    return &s->output_mgr;
}

/** @internal @This returns the private upipe_ts_demux_program structure.
 *
 * @param output_mgr public output_mgr structure of the pipe
 * @return pointer to the private upipe_ts_demux_program structure
 */
static inline struct upipe_ts_demux_program *
    upipe_ts_demux_program_from_output_mgr(struct upipe_mgr *output_mgr)
{
    return container_of(output_mgr, struct upipe_ts_demux_program, output_mgr);
}

/** @This returns the high-level upipe_ts_demux_program structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * upipe_ts_demux_program
 * @return pointer to the upipe_ts_demux_program structure
 */
static inline struct upipe_ts_demux_program *
    upipe_ts_demux_program_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upipe_ts_demux_program, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param upipe_ts_demux_program upipe_ts_demux_program structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    upipe_ts_demux_program_to_uchain(struct upipe_ts_demux_program *upipe_ts_demux_program)
{
    return &upipe_ts_demux_program->uchain;
}

/** @internal @This is the private context of an output of a ts_demux_program
 * subpipe. */
struct upipe_ts_demux_output {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** flow definition set */
    struct uref *flow_def;
    /** PID */
    uint64_t pid;
    /** ts_split_output subpipe */
    struct upipe *split_output;

    /** probe to get events from subpipes */
    struct uprobe plumber;

    /** pointer to the last subpipe */
    struct upipe *last_subpipe;
    /** pointer to the output of the last subpipe */
    struct upipe *output;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux_output, upipe)

/** @This returns the high-level upipe_ts_demux_output structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * upipe_ts_demux_output
 * @return pointer to the upipe_ts_demux_output structure
 */
static inline struct upipe_ts_demux_output *
    upipe_ts_demux_output_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upipe_ts_demux_output, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param upipe_ts_demux_output upipe_ts_demux_output structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    upipe_ts_demux_output_to_uchain(struct upipe_ts_demux_output *upipe_ts_demux_output)
{
    return &upipe_ts_demux_output->uchain;
}


/*
 * psi_pid structure handling
 */

/** @internal @This is the context of a PID carrying PSI of a ts_demux pipe. */
struct upipe_ts_demux_psi_pid {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** PID */
    uint16_t pid;
    /** pointer to psi_split subpipe */
    struct upipe *psi_split;
    /** pointer to split_output subpipe */
    struct upipe *split_output;
    /** reference count */
    unsigned int refcount;
};

/** @internal @This returns the uchain for chaining PIDs.
 *
 * @param psi_pid pointer to the upipe_ts_demux_psi_pid structure
 * @return pointer to uchain
 */
static inline struct uchain *
    upipe_ts_demux_psi_pid_to_uchain(struct upipe_ts_demux_psi_pid *psi_pid)
{
    return &psi_pid->uchain;
}

/** @internal @This returns the upipe_ts_demux_psi_pid structure.
 *
 * @param uchain pointer to uchain
 * @return pointer to the upipe_ts_demux_psi_pid structure
 */
static inline struct upipe_ts_demux_psi_pid *
    upipe_ts_demux_psi_pid_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upipe_ts_demux_psi_pid, uchain);
}

/** @internal @This allocates and initializes a new PID-specific
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @return pointer to allocated substructure
 */
static struct upipe_ts_demux_psi_pid *
    upipe_ts_demux_psi_pid_alloc(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_ts_demux_psi_pid *psi_pid =
        malloc(sizeof(struct upipe_ts_demux_psi_pid));
    if (unlikely(psi_pid == NULL))
        return NULL;
    psi_pid->pid = pid;

    /* allocate ts_psi_split subpipe */
    psi_pid->psi_split = upipe_alloc(ts_demux_mgr->ts_psi_split_mgr,
                                     upipe->uprobe,
                                     ulog_sub_alloc_va(upipe->ulog,
                                        ULOG_DEBUG, "psi_split %"PRIu16, pid));
    if (unlikely(psi_pid->psi_split == NULL)) {
        free(psi_pid);
        return NULL;
    }

    /* set PID filter on ts_split subpipe */
    psi_pid->split_output =
        upipe_alloc_output(upipe_ts_demux->split,
                           &upipe_ts_demux->psi_pid_plumber,
                           ulog_sub_alloc_va(upipe->ulog,
                                ULOG_DEBUG, "split output %"PRIu16, pid));
    if (unlikely(psi_pid->split_output == NULL)) {
        upipe_release(psi_pid->psi_split);
        free(psi_pid);
        return NULL;
    }

    struct uref *uref = uref_block_flow_alloc_def(upipe_ts_demux->uref_mgr,
                                                  "mpegts.mpegtspsi.");
    if (unlikely(uref == NULL || !uref_ts_flow_set_pid(uref, pid) ||
                 !upipe_set_flow_def(psi_pid->split_output, uref))) {
        if (uref != NULL)
            uref_free(uref);
        upipe_release(psi_pid->split_output);
        upipe_release(psi_pid->psi_split);
        free(psi_pid);
        return NULL;
    }
    uref_free(uref);
    psi_pid->refcount = 1;
    uchain_init(upipe_ts_demux_psi_pid_to_uchain(psi_pid));
    ulist_add(&upipe_ts_demux->psi_pids,
              upipe_ts_demux_psi_pid_to_uchain(psi_pid));
    return psi_pid;
}

/** @internal @This finds a psi_pid by its number.
 *
 * @param upipe description structure of the pipe
 * @param psi_pid_number psi_pid number (service ID)
 * @return pointer to substructure
 */
static struct upipe_ts_demux_psi_pid *
    upipe_ts_demux_psi_pid_find(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach(&upipe_ts_demux->psi_pids, uchain) {
        struct upipe_ts_demux_psi_pid *psi_pid =
            upipe_ts_demux_psi_pid_from_uchain(uchain);
        if (psi_pid->pid == pid)
            return psi_pid;
    }
    return NULL;
}

/** @internal @This marks a PID as being used for PSI, optionally allocates
 * the substructure, and increments the refcount.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @return pointer to substructure
 */
static struct upipe_ts_demux_psi_pid *
    upipe_ts_demux_psi_pid_use(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_demux_psi_pid *psi_pid =
        upipe_ts_demux_psi_pid_find(upipe, pid);
    if (psi_pid == NULL)
        return upipe_ts_demux_psi_pid_alloc(upipe, pid);

    psi_pid->refcount++;
    return psi_pid;
}

/** @internal @This releases a PID from being used for PSI, optionally
 * freeing allocated resources.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 */
static void upipe_ts_demux_psi_pid_release(struct upipe *upipe,
                                       struct upipe_ts_demux_psi_pid *psi_pid)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    assert(psi_pid != NULL);

    psi_pid->refcount--;
    if (!psi_pid->refcount) {
        struct uchain *uchain;
        ulist_delete_foreach(&upipe_ts_demux->psi_pids, uchain) {
            if (uchain == upipe_ts_demux_psi_pid_to_uchain(psi_pid)) {
                ulist_delete(&upipe_ts_demux->psi_pids, uchain);
            }
        }
        upipe_release(psi_pid->split_output);
        upipe_release(psi_pid->psi_split);
        free(psi_pid);
    }
}


/*
 * upipe_ts_demux_output structure handling (derived from upipe structure)
 */

/** @internal @This catches need_output events coming from output subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_output
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_output_plumber(struct uprobe *uprobe,
                                          struct upipe *subpipe,
                                          enum uprobe_event event,
                                          va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        container_of(uprobe, struct upipe_ts_demux_output, plumber);
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_output_mgr(
                upipe_ts_demux_program_to_upipe(program)->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, subpipe, event, args, &flow_def, &def))
        return false;

    if (subpipe == upipe_ts_demux_output->last_subpipe) {
        if (upipe_ts_demux_output->output != NULL)
            upipe_set_output(subpipe, upipe_ts_demux_output->output);
        else
            upipe_throw_need_output(upipe, flow_def);
        return true;
    }

    if (ubase_ncmp(def, "block."))
        return false;

    if (!ubase_ncmp(def, "block.mpegts.")) {
        /* allocate ts_decaps subpipe */
        struct upipe *output = upipe_alloc(ts_demux_mgr->ts_decaps_mgr,
                                           uprobe,
                                           ulog_sub_alloc(upipe->ulog,
                                                ULOG_DEBUG, "decaps"));
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.mpegtspes.")) {
        /* allocate ts_pesd subpipe */
        struct upipe *output = upipe_alloc(ts_demux_mgr->ts_pesd_mgr,
                                           uprobe,
                                           ulog_sub_alloc(upipe->ulog,
                                                ULOG_DEBUG, "pesd"));
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.mpeg2video.")) {
        return true;
        /* allocate mp2vf subpipe */
        struct upipe *output = upipe_alloc(ts_demux_mgr->mp2vf_mgr,
                                           &upipe_ts_demux_output->plumber,
                                           ulog_sub_alloc(upipe->ulog,
                                                ULOG_DEBUG, "mp2vf"));
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(subpipe, output);
            upipe_ts_demux_output->last_subpipe = output;
        }
        return true;
    }

    return false;
}

/** @internal @This allocates an output subpipe of a ts_demux_program subpipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_demux_output_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 struct ulog *ulog)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        malloc(sizeof(struct upipe_ts_demux_output));
    if (unlikely(upipe_ts_demux_output == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);
    upipe_init(upipe, mgr, uprobe, ulog);
    uchain_init(&upipe_ts_demux_output->uchain);
    upipe_ts_demux_output->flow_def = NULL;
    upipe_ts_demux_output->pid = 0;
    upipe_ts_demux_output->split_output = NULL;
    upipe_ts_demux_output->last_subpipe = upipe_ts_demux_output->output = NULL;
    uprobe_init(&upipe_ts_demux_output->plumber,
                upipe_ts_demux_output_plumber, upipe->uprobe);
    urefcount_init(&upipe_ts_demux_output->refcount);

    /* add the newly created output to the outputs list */
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(mgr);
    ulist_add(&program->outputs,
              upipe_ts_demux_output_to_uchain(upipe_ts_demux_output));

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This gets the flow definition on an output.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the flow definition packet
 * @return false in case of error
 */
static bool upipe_ts_demux_output_get_flow_def(struct upipe *upipe,
                                               struct uref **p)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    *p = upipe_ts_demux_output->flow_def;
    return true;
}

/** @internal @This sets the flow definition on an output.
 *
 * The attribute t.pid must be set on the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false in case of error
 */
static bool upipe_ts_demux_output_set_flow_def(struct upipe *upipe,
                                               struct uref *flow_def)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);
    struct upipe_ts_demux *demux =
        upipe_ts_demux_from_output_mgr(
                upipe_ts_demux_program_to_upipe(program)->mgr);

    if (upipe_ts_demux_output->split_output != NULL) {
        /* check if we can reuse the same split output */
        uint64_t pid;
        const char *def, *old_def;
        if (unlikely(!uref_ts_flow_get_pid(flow_def, &pid) || pid >= MAX_PIDS ||
                     !uref_flow_get_raw_def(flow_def, &def) ||
                     !uref_flow_get_raw_def(upipe_ts_demux_output->flow_def,
                                            &old_def)))
            return false;
        if (pid == upipe_ts_demux_output->pid && !strcmp(def, old_def)) {
            struct uref *flow_def_dup = uref_dup(flow_def);
            struct uref *uref = uref_dup(flow_def);
            if (likely(flow_def_dup != NULL && uref != NULL &&
                       uref_flow_set_def(uref, def) &&
                       uref_flow_delete_raw_def(uref))) {
                uref_free(upipe_ts_demux_output->flow_def);
                upipe_ts_demux_output->flow_def = flow_def_dup;
                upipe_set_flow_def(upipe_ts_demux_output->split_output, uref);
                uref_free(uref);
                return true;
            }
            if (flow_def_dup != NULL)
                uref_free(flow_def_dup);
            if (uref != NULL)
                uref_free(uref);
        }

        upipe_release(upipe_ts_demux_output->split_output);
        upipe_ts_demux_output->split_output = NULL;
    }
    if (upipe_ts_demux_output->last_subpipe != NULL) {
        upipe_release(upipe_ts_demux_output->last_subpipe);
        upipe_ts_demux_output->last_subpipe = NULL;
    }
    if (upipe_ts_demux_output->flow_def != NULL) {
        uref_free(upipe_ts_demux_output->flow_def);
        upipe_ts_demux_output->flow_def = NULL;
    }
    upipe_ts_demux_output->pid = 0;

    if (unlikely(!uref_ts_flow_get_pid(flow_def, &upipe_ts_demux_output->pid) ||
                 upipe_ts_demux_output->pid >= MAX_PIDS))
        return false;

    upipe_ts_demux_output->flow_def = uref_dup(flow_def);
    struct uref *uref = uref_dup(flow_def);
    const char *def;
    if (unlikely(upipe_ts_demux_output->flow_def == NULL ||
                 uref == NULL ||
                 !uref_flow_get_raw_def(flow_def, &def) ||
                 !uref_flow_set_def(uref, def) ||
                 !uref_flow_delete_raw_def(uref))) {
        if (upipe_ts_demux_output->flow_def != NULL) {
            uref_free(upipe_ts_demux_output->flow_def);
            upipe_ts_demux_output->flow_def = NULL;
        }
        if (uref != NULL)
            uref_free(uref);
        return false;
    }

    /* set up a split_output subpipe */
    upipe_ts_demux_output->split_output =
        upipe_alloc_output(demux->split, &upipe_ts_demux_output->plumber,
                           ulog_sub_alloc_va(upipe->ulog,
                                ULOG_DEBUG, "split output %"PRIu64,
                                upipe_ts_demux_output->pid));
    if (unlikely(upipe_ts_demux_output->split_output == NULL)) {
        uref_free(upipe_ts_demux_output->flow_def);
        upipe_ts_demux_output->flow_def = NULL;
        uref_free(uref);
        return false;
    }

    upipe_set_flow_def(upipe_ts_demux_output->split_output, uref);
    uref_free(uref);
    return true;
}

/** @internal @This gets the output pipe on an output.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with a pointer to the output
 * @return false in case of error
 */
static bool upipe_ts_demux_output_get_output(struct upipe *upipe,
                                             struct upipe **p)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    *p = upipe_ts_demux_output->output;
    return true;
}

/** @internal @This sets the output pipe on an output.
 *
 * @param upipe description structure of the pipe
 * @param output pointer to output
 * @return false in case of error
 */
static bool upipe_ts_demux_output_set_output(struct upipe *upipe,
                                             struct upipe *output)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    bool ret;
    if (upipe_ts_demux_output->output != NULL) {
        upipe_release(upipe_ts_demux_output->output);
        upipe_ts_demux_output->output = NULL;
    }

    if (upipe_ts_demux_output->last_subpipe != NULL)
        ret = upipe_set_output(upipe_ts_demux_output->last_subpipe, output);
    else
        ret = true;

    if (likely(ret)) {
        upipe_ts_demux_output->output = output;
        if (output != NULL)
            upipe_use(output);
    }
    return ret;
}

/** @internal @This processes control commands on an output subpipe of a
 * ts_demux_program subpipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_demux_output_control(struct upipe *upipe,
                                          enum upipe_command command,
                                          va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_demux_output_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_demux_output_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_demux_output_set_output(upipe, output);
        }

        default:
            return false;
    }
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_output_use(struct upipe *upipe)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    urefcount_use(&upipe_ts_demux_output->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_output_release(struct upipe *upipe)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_demux_output->refcount))) {
        struct upipe_ts_demux_program *program =
            upipe_ts_demux_program_from_output_mgr(upipe->mgr);
        upipe_throw_dead(upipe);

        /* remove output from the outputs list */
        struct uchain *uchain;
        ulist_delete_foreach(&program->outputs, uchain) {
            if (upipe_ts_demux_output_from_uchain(uchain) ==
                    upipe_ts_demux_output) {
                ulist_delete(&program->outputs, uchain);
                break;
            }
        }

        if (upipe_ts_demux_output->split_output != NULL)
            upipe_release(upipe_ts_demux_output->split_output);
        if (upipe_ts_demux_output->last_subpipe != NULL)
            upipe_release(upipe_ts_demux_output->last_subpipe);
        if (upipe_ts_demux_output->output != NULL)
            upipe_release(upipe_ts_demux_output->output);
        if (upipe_ts_demux_output->flow_def != NULL)
            uref_free(upipe_ts_demux_output->flow_def);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_demux_output->refcount);
        free(upipe_ts_demux_output);
    }
}

/** @This increments the reference count of a upipe manager.
 *
 * @param mgr pointer to upipe manager
 */
static void upipe_ts_demux_output_mgr_use(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(mgr);
    upipe_use(upipe_ts_demux_program_to_upipe(program));
}

/** @This decrements the reference count of a upipe manager or frees it.
 *
 * @param mgr pointer to upipe manager.
 */
static void upipe_ts_demux_output_mgr_release(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(mgr);
    upipe_release(upipe_ts_demux_program_to_upipe(program));
}

/** @internal @This initializes the output manager for a ts_demux_program
 * subpipe.
 *
 * @param upipe description structure of the pipe
 * @return pointer to output upipe manager
 */
static struct upipe_mgr *
    upipe_ts_demux_program_init_output_mgr(struct upipe *upipe)
{
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct upipe_mgr *output_mgr = &program->output_mgr;
    output_mgr->signature = UPIPE_TS_DEMUX_OUTPUT_SIGNATURE;
    output_mgr->upipe_alloc = upipe_ts_demux_output_alloc;
    output_mgr->upipe_input = NULL;
    output_mgr->upipe_control = upipe_ts_demux_output_control;
    output_mgr->upipe_use = upipe_ts_demux_output_use;
    output_mgr->upipe_release = upipe_ts_demux_output_release;
    output_mgr->upipe_mgr_use = upipe_ts_demux_output_mgr_use;
    output_mgr->upipe_mgr_release = upipe_ts_demux_output_mgr_release;
    return output_mgr;
}


/*
 * upipe_ts_demux_program structure handling (derived from upipe structure)
 */

/** @internal @This catches need_output events coming from program subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_program_plumber(struct uprobe *uprobe,
                                           struct upipe *subpipe,
                                           enum uprobe_event event,
                                           va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, plumber);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_output_mgr(upipe->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, subpipe, event, args, &flow_def, &def))
        return false;

    if (!ubase_ncmp(def, "block.mpegtspsi.mpegtspmt.")) {
        /* allocate ts_pmtd subpipe */
        struct upipe *output = upipe_alloc(ts_demux_mgr->ts_pmtd_mgr,
                                           &upipe_ts_demux_program->pmtd_probe,
                                           ulog_sub_alloc(upipe->ulog,
                                                ULOG_DEBUG, "pmtd"));
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    return false;
}

/** @internal @This catches events coming from pmtd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param pmtd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_program_pmtd_probe(struct uprobe *uprobe,
                                              struct upipe *pmtd,
                                             enum uprobe_event event,
                                             va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pmtd_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_output_mgr(upipe->mgr);

    switch (event) {
        case UPROBE_TS_PMTD_HEADER: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pmtd_pcrpid = va_arg(args, unsigned int);
            unsigned int pmtd_desc_offset = va_arg(args, unsigned int);
            unsigned int pmtd_desc_size = va_arg(args, unsigned int);
            upipe_ts_demux_program->pcr_pid = pmtd_pcrpid;
            return false;
        }
        case UPROBE_TS_PMTD_ADD_ES: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pid = va_arg(args, unsigned int);
            unsigned int streamtype = va_arg(args, unsigned int);
            unsigned int pmtd_desc_offset = va_arg(args, unsigned int);
            unsigned int pmtd_desc_size = va_arg(args, unsigned int);

            switch (streamtype) {
                case 0x2: {
                    struct uref *flow_def =
                        uref_block_flow_alloc_def(demux->uref_mgr,
                                                  "mpeg2video.");
                    if (likely(flow_def != NULL &&
                               uref_flow_set_raw_def(flow_def,
                                   "block.mpegts.mpegtspes.mpeg2video.") &&
                               uref_ts_flow_set_pid(flow_def, pid) &&
                               uref_flow_set_program_va(flow_def, "%u,",
                                   upipe_ts_demux_program->program)))
                        upipe_split_throw_add_flow(
                               upipe_ts_demux_to_upipe(demux), pid, flow_def);

                    if (flow_def != NULL)
                        uref_free(flow_def);
                    break;
                }
                default:
                    ulog_warning(upipe->ulog, "unknown stream type %u",
                                 streamtype);
                    break;
            }
            /* return false in case someone else is interested */
            return false;
        }
        case UPROBE_TS_PMTD_DEL_ES: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pid = va_arg(args, unsigned int);

            upipe_split_throw_del_flow(upipe_ts_demux_to_upipe(demux), pid);

            struct uchain *uchain;
            struct upipe_ts_demux_output *output = NULL;
            ulist_foreach (&upipe_ts_demux_program->outputs, uchain) {
                if (output != NULL)
                    upipe_release(upipe_ts_demux_output_to_upipe(output));
                output = upipe_ts_demux_output_from_uchain(uchain);
                /* to avoid having the uchain disappear during
                 * upipe_throw_read_end */
                upipe_use(upipe_ts_demux_output_to_upipe(output));
                if (output->pid == pid)
                    upipe_throw_read_end(upipe_ts_demux_output_to_upipe(output),
                                         NULL);
            }
            if (output != NULL)
                upipe_release(upipe_ts_demux_output_to_upipe(output));
            /* return false in case someone else is interested */
            return false;
        }
        default:
            return false;
    }
}

/** @internal @This allocates a program subpipe of a ts_demux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_demux_program_alloc(struct upipe_mgr *mgr,
                                                  struct uprobe *uprobe,
                                                  struct ulog *ulog)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        malloc(sizeof(struct upipe_ts_demux_program));
    if (unlikely(upipe_ts_demux_program == NULL))
        return NULL;
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);
    upipe_split_init(upipe, mgr, uprobe, ulog,
                     upipe_ts_demux_program_init_output_mgr(upipe));
    uchain_init(&upipe_ts_demux_program->uchain);
    upipe_ts_demux_program->flow_def = NULL;
    upipe_ts_demux_program->program = 0;
    upipe_ts_demux_program->pcr_pid = 0;
    upipe_ts_demux_program->psi_split_output = NULL;
    uprobe_init(&upipe_ts_demux_program->plumber,
                upipe_ts_demux_program_plumber, upipe->uprobe);
    uprobe_init(&upipe_ts_demux_program->pmtd_probe,
                upipe_ts_demux_program_pmtd_probe, upipe->uprobe);
    ulist_init(&upipe_ts_demux_program->outputs);
    urefcount_init(&upipe_ts_demux_program->refcount);

    /* add the newly created program to the programs list */
    struct upipe_ts_demux *demux = upipe_ts_demux_from_output_mgr(mgr);
    ulist_add(&demux->programs,
              upipe_ts_demux_program_to_uchain(upipe_ts_demux_program));

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This gets the flow definition on a program.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the flow definition packet
 * @return false in case of error
 */
static bool upipe_ts_demux_program_get_flow_def(struct upipe *upipe,
                                                struct uref **p)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    *p = upipe_ts_demux_program->flow_def;
    return true;
}

/** @internal @This sets the flow definition on a program.
 *
 * The attribute t.psi.filter must be set on the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false in case of error
 */
static bool upipe_ts_demux_program_set_flow_def(struct upipe *upipe,
                                                struct uref *flow_def)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_output_mgr(upipe->mgr);
    if (!ulist_empty(&upipe_ts_demux_program->outputs))
        return false;

    if (upipe_ts_demux_program->psi_split_output != NULL) {
        upipe_release(upipe_ts_demux_program->psi_split_output);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
        upipe_ts_demux_program->psi_split_output = NULL;
        upipe_ts_demux_program->psi_pid = NULL;
    }
    if (upipe_ts_demux_program->flow_def != NULL) {
        uref_free(upipe_ts_demux_program->flow_def);
        upipe_ts_demux_program->flow_def = NULL;
    }
    upipe_ts_demux_program->program = 0;
    upipe_ts_demux_program->pcr_pid = 0;

    uint64_t pid;
    const char *program;
    const uint8_t *filter, *mask;
    size_t size;
    if (unlikely(!uref_ts_flow_get_pid(flow_def, &pid) || pid >= MAX_PIDS ||
                 !uref_ts_flow_get_psi_filter(flow_def, &filter, &mask,
                                              &size) ||
                 !uref_flow_get_program(flow_def, &program) ||
                 sscanf(program, "%u,",
                        &upipe_ts_demux_program->program) != 1 ||
                 upipe_ts_demux_program->program == 0 ||
                 upipe_ts_demux_program->program > UINT16_MAX)) {
        upipe_ts_demux_program->program = 0;
        return false;
    }

    upipe_ts_demux_program->flow_def = uref_dup(flow_def);
    struct uref *uref = uref_dup(flow_def);
    const char *def;
    if (unlikely(upipe_ts_demux_program->flow_def == NULL ||
                 uref == NULL ||
                 !uref_flow_get_raw_def(flow_def, &def) ||
                 !uref_flow_set_def(uref, def) ||
                 !uref_flow_delete_raw_def(uref))) {
        if (upipe_ts_demux_program->flow_def != NULL) {
            uref_free(upipe_ts_demux_program->flow_def);
            upipe_ts_demux_program->flow_def = NULL;
        }
        if (uref != NULL)
            uref_free(uref);
        return false;
    }

    /* set up a psi_split_output subpipe */
    upipe_ts_demux_program->psi_pid =
        upipe_ts_demux_psi_pid_use(upipe_ts_demux_to_upipe(demux), pid);
    if (unlikely(upipe_ts_demux_program->psi_pid == NULL)) {
        uref_free(upipe_ts_demux_program->flow_def);
        upipe_ts_demux_program->flow_def = NULL;
        uref_free(uref);
        return false;
    }
    upipe_ts_demux_program->psi_split_output =
        upipe_alloc_output(upipe_ts_demux_program->psi_pid->psi_split,
                           &upipe_ts_demux_program->plumber,
                           ulog_sub_alloc(upipe->ulog,
                                ULOG_DEBUG, "psi_split output"));
    if (unlikely(upipe_ts_demux_program->psi_split_output == NULL)) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
        upipe_ts_demux_program->psi_pid = NULL;
        uref_free(upipe_ts_demux_program->flow_def);
        upipe_ts_demux_program->flow_def = NULL;
        uref_free(uref);
        return false;
    }

    upipe_set_flow_def(upipe_ts_demux_program->psi_split_output, uref);
    uref_free(uref);
    return true;
}

/** @internal @This processes control commands on a program subpipe of a
 * ts_demux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_demux_program_control(struct upipe *upipe,
                                           enum upipe_command command,
                                           va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_demux_program_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_demux_program_set_flow_def(upipe, flow_def);
        }

        default:
            return false;
    }
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_use(struct upipe *upipe)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    urefcount_use(&upipe_ts_demux_program->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_release(struct upipe *upipe)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_demux_program->refcount))) {
        struct upipe_ts_demux *demux =
            upipe_ts_demux_from_output_mgr(upipe->mgr);
        upipe_throw_dead(upipe);

        /* remove program from the programs list */
        struct uchain *uchain;
        ulist_delete_foreach(&demux->programs, uchain) {
            if (upipe_ts_demux_program_from_uchain(uchain) ==
                    upipe_ts_demux_program) {
                ulist_delete(&demux->programs, uchain);
                break;
            }
        }

        if (upipe_ts_demux_program->psi_split_output != NULL) {
            upipe_release(upipe_ts_demux_program->psi_split_output);
            upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                           upipe_ts_demux_program->psi_pid);
        }
        if (upipe_ts_demux_program->flow_def != NULL)
            uref_free(upipe_ts_demux_program->flow_def);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_demux_program->refcount);
        free(upipe_ts_demux_program);
    }
}

/** @This increments the reference count of a upipe manager.
 *
 * @param mgr pointer to upipe manager
 */
static void upipe_ts_demux_program_mgr_use(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_output_mgr(mgr);
    upipe_use(upipe_ts_demux_to_upipe(upipe_ts_demux));
}

/** @This decrements the reference count of a upipe manager or frees it.
 *
 * @param mgr pointer to upipe manager.
 */
static void upipe_ts_demux_program_mgr_release(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_output_mgr(mgr);
    upipe_release(upipe_ts_demux_to_upipe(upipe_ts_demux));
}

/** @internal @This initializes the output manager for a ts_demux pipe.
 *
 * @param upipe description structure of the pipe
 * @return pointer to output upipe manager
 */
static struct upipe_mgr *upipe_ts_demux_init_output_mgr(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct upipe_mgr *output_mgr = &upipe_ts_demux->output_mgr;
    output_mgr->signature = UPIPE_TS_DEMUX_PROGRAM_SIGNATURE;
    output_mgr->upipe_alloc = upipe_ts_demux_program_alloc;
    output_mgr->upipe_input = NULL;
    output_mgr->upipe_control = upipe_ts_demux_program_control;
    output_mgr->upipe_use = upipe_ts_demux_program_use;
    output_mgr->upipe_release = upipe_ts_demux_program_release;
    output_mgr->upipe_mgr_use = upipe_ts_demux_program_mgr_use;
    output_mgr->upipe_mgr_release = upipe_ts_demux_program_mgr_release;
    return output_mgr;
}


/*
 * upipe_ts_demux structure handling (derived from upipe structure)
 */

/** @internal @This catches need_output events coming from subpipes created by
 * psi_pid objects.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param subpipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_psi_pid_plumber(struct uprobe *uprobe,
                                           struct upipe *subpipe,
                                           enum uprobe_event event,
                                           va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, psi_pid_plumber);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, subpipe, event, args, &flow_def, &def))
        return false;

    if (ubase_ncmp(def, "block."))
        return false;

    if (!ubase_ncmp(def, "block.mpegts.")) {
        /* allocate ts_decaps subpipe */
        struct upipe *output = upipe_alloc(ts_demux_mgr->ts_decaps_mgr,
                                           uprobe,
                                           ulog_sub_alloc(upipe->ulog,
                                                ULOG_DEBUG, "decaps"));
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.mpegtspsi.")) {
        /* allocate ts_psim subpipe */
        struct upipe *output = upipe_alloc(ts_demux_mgr->ts_psim_mgr,
                                           &upipe_ts_demux->psim_plumber,
                                           ulog_sub_alloc(upipe->ulog,
                                                ULOG_DEBUG, "psim"));
        if (unlikely(output == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    return false;
}

/** @internal @This catches need_output events coming from psim subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_psim_plumber(struct uprobe *uprobe,
                                        struct upipe *psim,
                                        enum uprobe_event event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, psim_plumber);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, psim, event, args, &flow_def, &def))
        return false;

    uint64_t pid;
    if (unlikely(!uref_ts_flow_get_pid(flow_def, &pid))) {
        ulog_warning(upipe->ulog, "invalid flow definition");
        return true;
    }

    struct upipe_ts_demux_psi_pid *psi_pid =
        upipe_ts_demux_psi_pid_find(upipe, pid);
    if (unlikely(psi_pid == NULL)) {
        ulog_warning(upipe->ulog, "unknown PSI PID %"PRIu64, pid);
        return true;
    }

    upipe_set_output(psim, psi_pid->psi_split);
    return true;
}

/** @internal @This tries to guess the conformance of the stream from the
 * information that is available to us.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_conformance_guess(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (!upipe_ts_demux->auto_conformance)
        return;

    switch (upipe_ts_demux->nit_pid) {
        default:
        case 0:
            /* No NIT yet, nothing to guess */
            upipe_ts_demux->conformance = CONFORMANCE_ISO;
            break;
        case 16:
            /* Mandatory PID in DVB systems */
            upipe_ts_demux->conformance = CONFORMANCE_DVB;
            break;
        case 0x1ffb:
            /* Discouraged use of the base PID as NIT in ATSC systems */
            upipe_ts_demux->conformance = CONFORMANCE_ATSC;
            break;
    }
}

/** @internal @This sets the PID of the NIT, and take appropriate actions.
 *
 * @param upipe description structure of the pipe
 * @param pid NIT PID
 */
static void upipe_ts_demux_nit_pid(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    upipe_ts_demux->nit_pid = pid;
    upipe_ts_demux_conformance_guess(upipe);
}

/** @internal @This catches events coming from patd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param patd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_patd_probe(struct uprobe *uprobe,
                                      struct upipe *patd,
                                      enum uprobe_event event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, patd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);
    switch (event) {
        case UPROBE_TS_PATD_ADD_PROGRAM: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int program = va_arg(args, unsigned int);
            unsigned int pid = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PATD_SIGNATURE);
            if (!program) {
                upipe_ts_demux_nit_pid(upipe, pid);
                return true;
            }

            /* set filter on table 2, current, program number */
            uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
            uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
            memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
            memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
            psi_set_syntax(filter);
            psi_set_syntax(mask);
            psi_set_tableid(filter, PMT_TABLE_ID);
            psi_set_tableid(mask, 0xff);
            psi_set_current(filter);
            psi_set_current(mask);
            psi_set_tableidext(filter, program);
            psi_set_tableidext(mask, 0xffff);
            struct uref *flow_def =
                uref_alloc_control(upipe_ts_demux->uref_mgr);

            if (likely(flow_def != NULL &&
                       uref_flow_set_def(flow_def, "internal.") &&
                       uref_flow_set_raw_def(flow_def,
                                             "block.mpegtspsi.mpegtspmt.") &&
                       uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                                   PSI_HEADER_SIZE_SYNTAX1) &&
                       uref_ts_flow_set_pid(flow_def, pid) &&
                       uref_flow_set_program_va(flow_def, "%u,", program)))
                upipe_split_throw_add_flow(upipe, program, flow_def);

            if (flow_def != NULL)
                uref_free(flow_def);
            /* return false in case someone else is interested */
            return true;
        }
        case UPROBE_TS_PATD_DEL_PROGRAM: {
            unsigned int signature = va_arg(args, unsigned int);
            struct uref *uref = va_arg(args, struct uref *);
            unsigned int pmtd_program = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PATD_SIGNATURE);

            upipe_split_throw_del_flow(upipe, pmtd_program);

            struct uchain *uchain;
            struct upipe_ts_demux_program *program = NULL;
            ulist_foreach (&upipe_ts_demux->programs, uchain) {
                if (program != NULL)
                    upipe_release(upipe_ts_demux_program_to_upipe(program));
                program = upipe_ts_demux_program_from_uchain(uchain);
                /* to avoid having the uchain disappear during
                 * upipe_throw_read_end */
                upipe_use(upipe_ts_demux_program_to_upipe(program));
                if (program->program == pmtd_program)
                    upipe_throw_read_end(upipe_ts_demux_program_to_upipe(program),
                                         NULL);
            }
            if (program != NULL)
                upipe_release(upipe_ts_demux_program_to_upipe(program));
            /* return false in case someone else is interested */
            return false;
        }
        default:
            return false;
    }
}

/** @internal @This allocates a ts_demux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_demux_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          struct ulog *ulog)
{
    struct upipe_ts_demux *upipe_ts_demux =
        malloc(sizeof(struct upipe_ts_demux));
    if (unlikely(upipe_ts_demux == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);
    upipe_split_init(upipe, mgr, uprobe, ulog,
                     upipe_ts_demux_init_output_mgr(upipe));
    upipe_ts_demux_init_uref_mgr(upipe);
    upipe_ts_demux->flow_def_ok = false;
    upipe_ts_demux->input_mode = UPIPE_TS_DEMUX_OFF;
    upipe_ts_demux->input = upipe_ts_demux->split = NULL;

    ulist_init(&upipe_ts_demux->psi_pids);
    upipe_ts_demux->conformance = CONFORMANCE_ISO;
    upipe_ts_demux->auto_conformance = true;
    upipe_ts_demux->nit_pid = 0;

    uprobe_init(&upipe_ts_demux->psi_pid_plumber,
                upipe_ts_demux_psi_pid_plumber, upipe->uprobe);
    uprobe_init(&upipe_ts_demux->psim_plumber, upipe_ts_demux_psim_plumber,
                upipe->uprobe);
    uprobe_init(&upipe_ts_demux->patd_probe,
                upipe_ts_demux_patd_probe, upipe->uprobe);

    ulist_init(&upipe_ts_demux->programs);

    urefcount_init(&upipe_ts_demux->refcount);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This starts the split pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_init(struct upipe *upipe)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    upipe_ts_demux->split = upipe_alloc(ts_demux_mgr->ts_split_mgr,
                                        upipe->uprobe,
                                        ulog_sub_alloc(upipe->ulog,
                                            ULOG_DEBUG, "split"));
    if (unlikely(upipe_ts_demux->split == NULL)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }

    /* get psi_split subpipe */
    upipe_ts_demux->psi_pid_pat = upipe_ts_demux_psi_pid_use(upipe, 0);
    if (unlikely(upipe_ts_demux->psi_pid_pat == NULL)) {
        upipe_release(upipe_ts_demux->split);
        upipe_ts_demux->split = NULL;
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }

    upipe_ts_demux->psi_split_output_pat =
        upipe_alloc_output(upipe_ts_demux->psi_pid_pat->psi_split,
                           upipe->uprobe,
                           ulog_sub_alloc(upipe->ulog,
                                ULOG_DEBUG, "psi_split output"));
    if (unlikely(upipe_ts_demux->psi_split_output_pat == NULL)) {
        upipe_ts_demux_psi_pid_release(upipe, upipe_ts_demux->psi_pid_pat);
        upipe_release(upipe_ts_demux->split);
        upipe_ts_demux->split = NULL;
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }

    /* allocate PAT decoder */
    struct upipe *patd = upipe_alloc(ts_demux_mgr->ts_patd_mgr,
                                     &upipe_ts_demux->patd_probe,
                                       ulog_sub_alloc(upipe->ulog,
                                            ULOG_DEBUG, "patd"));
    if (unlikely(patd == NULL)) {
        upipe_release(upipe_ts_demux->psi_split_output_pat);
        upipe_ts_demux_psi_pid_release(upipe, upipe_ts_demux->psi_pid_pat);
        upipe_release(upipe_ts_demux->split);
        upipe_ts_demux->split = NULL;
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
    }
    upipe_set_output(upipe_ts_demux->psi_split_output_pat, patd);
    upipe_release(patd);

    /* set filter on table 0, current */
    uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
    uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
    memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
    memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
    psi_set_syntax(filter);
    psi_set_syntax(mask);
    psi_set_tableid(filter, PAT_TABLE_ID);
    psi_set_tableid(mask, 0xff);
    psi_set_current(filter);
    psi_set_current(mask);
    struct uref *flow_def = uref_block_flow_alloc_def(upipe_ts_demux->uref_mgr,
                                                      "mpegtspsi.mpegtspat.");
    if (unlikely(flow_def == NULL ||
                 !uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE_SYNTAX1) ||
                 !uref_ts_flow_set_pid(flow_def, 0) ||
                 !upipe_set_flow_def(upipe_ts_demux->psi_split_output_pat,
                                     flow_def))) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_release(upipe_ts_demux->psi_split_output_pat);
        upipe_ts_demux_psi_pid_release(upipe, upipe_ts_demux->psi_pid_pat);
        upipe_release(upipe_ts_demux->split);
        upipe_ts_demux->split = NULL;
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }
    uref_free(flow_def);
}

/** @internal @This sets the input mode.
 *
 * @param upipe description structure of the pipe
 * @param input_mode input mode
 */
static void upipe_ts_demux_set_input_mode(struct upipe *upipe,
                                          enum upipe_ts_demux_mode input_mode)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->input_mode != UPIPE_TS_DEMUX_OFF)
        upipe_release(upipe_ts_demux->input);
    upipe_ts_demux->input_mode = input_mode;
    if (input_mode == UPIPE_TS_DEMUX_OFF) {
        upipe_ts_demux->input = NULL;
        return;
    }

    switch (input_mode) {
        case UPIPE_TS_DEMUX_OFF:
            return; /* to shut gcc up */
        case UPIPE_TS_DEMUX_SYNC:
            upipe_ts_demux->input = upipe_ts_demux->split;
            upipe_use(upipe_ts_demux->input);
            return;

        case UPIPE_TS_DEMUX_CHECK:
            /* allocate ts_check subpipe */
            upipe_ts_demux->input = upipe_alloc(ts_demux_mgr->ts_check_mgr,
                                                upipe->uprobe,
                                                ulog_sub_alloc(upipe->ulog,
                                                    ULOG_DEBUG, "check"));
            break;

        case UPIPE_TS_DEMUX_SCAN:
            /* allocate ts_scan subpipe */
            upipe_ts_demux->input = upipe_alloc(ts_demux_mgr->ts_sync_mgr,
                                                upipe->uprobe,
                                                ulog_sub_alloc(upipe->ulog,
                                                    ULOG_DEBUG, "sync"));
            break;
    }
    if (unlikely(upipe_ts_demux->input == NULL)) {
        upipe_ts_demux->input_mode = UPIPE_TS_DEMUX_OFF;
        return;
    }
    upipe_set_output(upipe_ts_demux->input, upipe_ts_demux->split);
}

/** @internal @This demuxes a TS packet to the appropriate output(s).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_demux_work(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (unlikely(upipe_ts_demux->input_mode == UPIPE_TS_DEMUX_OFF)) {
        uref_free(uref);
        return;
    }
    upipe_input(upipe_ts_demux->input, uref, upump);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_demux_input(struct upipe *upipe, struct uref *uref,
                                 struct upump *upump)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);

    if (upipe_ts_demux->uref_mgr == NULL) {
        upipe_throw_need_uref_mgr(upipe);
        if (unlikely(upipe_ts_demux->uref_mgr == NULL)) {
            uref_free(uref);
            return;
        }
    }
    if (upipe_ts_demux->split == NULL) {
        upipe_ts_demux_init(upipe);
        if (unlikely(upipe_ts_demux->split == NULL)) {
            uref_free(uref);
            return;
        }
    }

    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        enum upipe_ts_demux_mode input_mode;
        if (!ubase_ncmp(def, EXPECTED_FLOW_DEF_SYNC))
            input_mode = UPIPE_TS_DEMUX_SYNC;
        else if (!ubase_ncmp(def, EXPECTED_FLOW_DEF_CHECK))
            input_mode = UPIPE_TS_DEMUX_CHECK;
        else if (!ubase_ncmp(def, EXPECTED_FLOW_DEF))
            input_mode = UPIPE_TS_DEMUX_SCAN;
        else {
            uref_free(uref);
            upipe_ts_demux->flow_def_ok = false;
            upipe_throw_flow_def_error(upipe, uref);
            return;
        }

        ulog_debug(upipe->ulog, "flow definition: %s", def);
        upipe_ts_demux->flow_def_ok = true;
        upipe_ts_demux_set_input_mode(upipe, input_mode);
        upipe_ts_demux_work(upipe, uref, upump);
        return;
    }

    if (unlikely(!upipe_ts_demux->flow_def_ok)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

    upipe_ts_demux_work(upipe, uref, upump);
}

/** @internal @This returns the currently detected conformance mode. It cannot
 * return CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return false in case of error
 */
static bool _upipe_ts_demux_get_conformance(struct upipe *upipe,
                                enum upipe_ts_demux_conformance *conformance_p)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    assert(conformance_p != NULL);
    *conformance_p = upipe_ts_demux->conformance;
    return true;
}

/** @internal @This sets the conformance mode.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 * @return false in case of error
 */
static bool _upipe_ts_demux_set_conformance(struct upipe *upipe,
                                enum upipe_ts_demux_conformance conformance)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    switch (conformance) {
        case CONFORMANCE_AUTO:
            upipe_ts_demux->auto_conformance = true;
            upipe_ts_demux_conformance_guess(upipe);
            break;
        case CONFORMANCE_ISO:
        case CONFORMANCE_DVB:
        case CONFORMANCE_ATSC:
        case CONFORMANCE_ISDB:
            upipe_ts_demux->auto_conformance = false;
            upipe_ts_demux->conformance = conformance;
            break;
        default:
            return false;
    }
    return true;
}

/** @internal @This processes control commands on a ts_demux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_demux_control(struct upipe *upipe,
                                   enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_ts_demux_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_ts_demux_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_TS_DEMUX_GET_CONFORMANCE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_DEMUX_SIGNATURE);
            enum upipe_ts_demux_conformance *conformance_p =
                va_arg(args, enum upipe_ts_demux_conformance *);
            return _upipe_ts_demux_get_conformance(upipe, conformance_p);
        }
        case UPIPE_TS_DEMUX_SET_CONFORMANCE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_DEMUX_SIGNATURE);
            enum upipe_ts_demux_conformance conformance =
                va_arg(args, enum upipe_ts_demux_conformance);
            return _upipe_ts_demux_set_conformance(upipe, conformance);
        }

        default:
            return false;
    }
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_use(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    urefcount_use(&upipe_ts_demux->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_release(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_demux->refcount))) {
        upipe_throw_dead(upipe);

        if (upipe_ts_demux->split != NULL) {
            upipe_ts_demux_set_input_mode(upipe, UPIPE_TS_DEMUX_OFF);
            upipe_release(upipe_ts_demux->psi_split_output_pat);
            upipe_ts_demux_psi_pid_release(upipe, upipe_ts_demux->psi_pid_pat);
            upipe_release(upipe_ts_demux->split);
        }
        upipe_ts_demux_clean_uref_mgr(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_demux->refcount);
        free(upipe_ts_demux);
    }
}

/** @This increments the reference count of a upipe manager.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_mgr_use(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(mgr);
    urefcount_use(&ts_demux_mgr->refcount);
}

/** @This decrements the reference count of a upipe manager or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_mgr_release(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(mgr);
    if (unlikely(urefcount_release(&ts_demux_mgr->refcount))) {
        upipe_mgr_release(ts_demux_mgr->ts_split_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_sync_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_check_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_decaps_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_psim_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_psi_split_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_patd_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_pmtd_mgr);
        upipe_mgr_release(ts_demux_mgr->ts_pesd_mgr);
        //upipe_mgr_release(ts_demux_mgr->mp2vf_mgr);

        urefcount_clean(&ts_demux_mgr->refcount);
        free(ts_demux_mgr);
    }
}

/** @This returns the management structure for all ts_demux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_demux_mgr_alloc(void)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        malloc(sizeof(struct upipe_ts_demux_mgr));
    if (unlikely(ts_demux_mgr == NULL))
        return NULL;

    ts_demux_mgr->ts_split_mgr = upipe_ts_split_mgr_alloc();
    ts_demux_mgr->ts_sync_mgr = upipe_ts_sync_mgr_alloc();
    ts_demux_mgr->ts_check_mgr = upipe_ts_check_mgr_alloc();
    ts_demux_mgr->ts_decaps_mgr = upipe_ts_decaps_mgr_alloc();
    ts_demux_mgr->ts_psim_mgr = upipe_ts_psim_mgr_alloc();
    ts_demux_mgr->ts_psi_split_mgr = upipe_ts_psi_split_mgr_alloc();
    ts_demux_mgr->ts_patd_mgr = upipe_ts_patd_mgr_alloc();
    ts_demux_mgr->ts_pmtd_mgr = upipe_ts_pmtd_mgr_alloc();
    ts_demux_mgr->ts_pesd_mgr = upipe_ts_pesd_mgr_alloc();
    ts_demux_mgr->mp2vf_mgr = NULL; //upipe_mp2vf_mgr_alloc();

    ts_demux_mgr->mgr.signature = UPIPE_TS_DEMUX_SIGNATURE;
    ts_demux_mgr->mgr.upipe_alloc = upipe_ts_demux_alloc;
    ts_demux_mgr->mgr.upipe_input = upipe_ts_demux_input;
    ts_demux_mgr->mgr.upipe_control = upipe_ts_demux_control;
    ts_demux_mgr->mgr.upipe_use = upipe_ts_demux_use;
    ts_demux_mgr->mgr.upipe_release = upipe_ts_demux_release;
    ts_demux_mgr->mgr.upipe_mgr_use = upipe_ts_demux_mgr_use;
    ts_demux_mgr->mgr.upipe_mgr_release = upipe_ts_demux_mgr_release;
    urefcount_init(&ts_demux_mgr->refcount);
    return upipe_ts_demux_mgr_to_upipe_mgr(ts_demux_mgr);
}

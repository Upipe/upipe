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
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_program_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_bin.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_setrap.h>
#include <upipe-modules/upipe_proxy.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-ts/upipe_ts_sync.h>
#include <upipe-ts/upipe_ts_check.h>
#include <upipe-ts/upipe_ts_decaps.h>
#include <upipe-ts/upipe_ts_psi_merge.h>
#include <upipe-ts/upipe_ts_psi_split.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>
#include <upipe-ts/upipe_ts_pes_decaps.h>

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
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define UINT33_MAX UINT64_C(8589934592)
/** max resolution of PCR, PTS and DTS */
#define TS_CLOCK_MAX (UINT33_MAX * UCLOCK_FREQ / 90000)
/** max interval between PCRs (ISO/IEC 13818-1 2.7.2) */
#define MAX_PCR_INTERVAL (UCLOCK_FREQ / 10)
/** max retention time for most streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY UCLOCK_FREQ
/** max retention time for ISO/IEC 14496 streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_14496 (UCLOCK_FREQ * 10)
/** max retention time for still pictures streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_STILL (UCLOCK_FREQ * 60)

/** @internal @This is the private context of a ts_demux manager. */
struct upipe_ts_demux_mgr {
    /** pointer to null manager */
    struct upipe_mgr *null_mgr;
    /** pointer to setrap manager */
    struct upipe_mgr *setrap_mgr;

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
    /** pointer to h264f manager */
    struct upipe_mgr *h264f_mgr;

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

/** @hidden */
struct upipe_ts_demux_psi_pid;

/** @internal @This is the private context of a ts_demux pipe. */
struct upipe_ts_demux {
    /** pointer to null subpipe */
    struct upipe *null;

    /** pointer to input subpipe */
    struct upipe *input;
    /** true if we have thrown the sync_acquired event */
    bool acquired;
    /** flow definition of the input */
    struct uref *flow_def_input;

    /** pointer to setrap subpipe */
    struct upipe *setrap;
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
    /** probe to get events from ts_psim subpipes created by psi_pid objects */
    struct uprobe psim_probe;
    /** probe to get events from ts_patd subpipe */
    struct uprobe patd_probe;
    /** probe to get events from ts_sync or ts_check subpipe */
    struct uprobe input_probe;
    /** probe to get events from ts_split subpipe */
    struct uprobe split_probe;

    /** list of programs */
    struct ulist programs;

    /** manager to create programs */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux, upipe)
UPIPE_HELPER_FLOW(upipe_ts_demux, EXPECTED_FLOW_DEF)
UPIPE_HELPER_SYNC(upipe_ts_demux, acquired)

/** @internal @This is the private context of a program of a ts_demux pipe. */
struct upipe_ts_demux_program {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** flow definition of the input */
    struct uref *flow_def_input;
    /** program number */
    unsigned int program;
    /** ts_psi_split_output subpipe */
    struct upipe *psi_split_output;
    /** pointer to psi_pid structure */
    struct upipe_ts_demux_psi_pid *psi_pid;
    /** systime_rap of the last PMT */
    uint64_t systime_pmt;
    /** systime_rap of the last PCR */
    uint64_t systime_pcr;

    /** PCR PID */
    uint16_t pcr_pid;
    /** PCR ts_split output subpipe */
    struct upipe *pcr_split_output;

    /** offset between MPEG timestamps and Upipe timestamps */
    int64_t timestamp_offset;
    /** last MPEG clock reference */
    uint64_t last_pcr;
    /** highest Upipe timestamp given to a frame */
    uint64_t timestamp_highest;

    /** probe to get events from subpipes */
    struct uprobe plumber;
    /** probe to get events from ts_pmtd subpipe */
    struct uprobe pmtd_probe;
    /** probe to get events from PCR ts_decaps subpipe */
    struct uprobe pcr_probe;

    /** list of outputs */
    struct ulist outputs;

    /** manager to create outputs */
    struct upipe_mgr output_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux_program, upipe)
UPIPE_HELPER_FLOW(upipe_ts_demux_program, "program.")

UPIPE_HELPER_SUBPIPE(upipe_ts_demux, upipe_ts_demux_program, program,
                     program_mgr, programs, uchain)

/** @internal @This is the private context of an output of a ts_demux_program
 * subpipe. */
struct upipe_ts_demux_output {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** PID */
    uint64_t pid;
    /** true if the output is used for PCR */
    bool pcr;
    /** ts_split_output subpipe */
    struct upipe *split_output;
    /** setrap subpipe */
    struct upipe *setrap;
    /** systime of the last random-access frame */
    uint64_t systime_random;

    /** maximum retention time in the pipeline */
    uint64_t max_delay;

    /** probe to get events from subpipes */
    struct uprobe probe;

    /** probe for the last subpipe */
    struct uprobe last_subpipe_probe;
    /** pointer to the last subpipe */
    struct upipe *last_subpipe;
    /** pointer to the output of the last subpipe */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux_output, upipe)
UPIPE_HELPER_FLOW(upipe_ts_demux_output, NULL)
UPIPE_HELPER_BIN(upipe_ts_demux_output, last_subpipe_probe, last_subpipe,
                 output)

UPIPE_HELPER_SUBPIPE(upipe_ts_demux_program, upipe_ts_demux_output, output,
                     output_mgr, outputs, uchain)


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

    /* set PID filter on ts_split subpipe */
    struct uref *flow_def = uref_dup(upipe_ts_demux->flow_def_input);
    if (unlikely(flow_def == NULL ||
                 !uref_flow_set_def(flow_def, "block.mpegtspsi.") ||
                 !uref_ts_flow_set_pid(flow_def, pid) ||
                 (psi_pid->psi_split =
                      upipe_flow_alloc(ts_demux_mgr->ts_psi_split_mgr,
                                       uprobe_pfx_adhoc_alloc_va(upipe->uprobe,
                                               UPROBE_LOG_DEBUG,
                                               "psi split %"PRIu16, pid),
                                       flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        free(psi_pid);
        return NULL;
    }

    uref_flow_set_def(flow_def, "block.mpegts.mpegtspsi.");
    psi_pid->split_output =
        upipe_flow_alloc_sub(upipe_ts_demux->split,
                             uprobe_pfx_adhoc_alloc_va(
                                 &upipe_ts_demux->psi_pid_plumber,
                                 UPROBE_LOG_DEBUG, "split output %"PRIu16, pid),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(psi_pid->split_output == NULL)) {
        upipe_release(psi_pid->psi_split);
        free(psi_pid);
        return NULL;
    }

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
    ulist_foreach (&upipe_ts_demux->psi_pids, uchain) {
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
        ulist_delete_foreach (&upipe_ts_demux->psi_pids, uchain) {
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

/** @hidden */
static void upipe_ts_demux_program_handle_pcr(struct upipe *upipe,
                                              struct uref *uref,
                                              uint64_t pcr_orig,
                                              int discontinuity);
/** @hidden */
static void upipe_ts_demux_program_check_pcr(struct upipe *upipe);

/** @internal @This catches clock_ref events coming from output subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_output
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_output_clock_ref(struct uprobe *uprobe,
                                            struct upipe *subpipe,
                                            enum uprobe_event event,
                                            va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        container_of(uprobe, struct upipe_ts_demux_output, probe);
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);

    if (upipe_ts_demux_output->pcr) {
        struct uref *uref = va_arg(args, struct uref *);
        uint64_t pcr_orig = va_arg(args, uint64_t);
        int discontinuity = va_arg(args, int);
        upipe_ts_demux_program_handle_pcr(
                upipe_ts_demux_program_to_upipe(program),
                uref, pcr_orig, discontinuity);
    }
    return true;
}

/** @internal @This catches clock_ts events coming from output subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_output
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_output_clock_ts(struct uprobe *uprobe,
                                           struct upipe *subpipe,
                                           enum uprobe_event event,
                                           va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        container_of(uprobe, struct upipe_ts_demux_output, probe);
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);

    struct uref *uref = va_arg(args, struct uref *);
    uint64_t pts_orig = UINT64_MAX, dts_orig = UINT64_MAX;
    uref_clock_get_pts_orig(uref, &pts_orig);
    uref_clock_get_dts_orig(uref, &dts_orig);
    if (pts_orig != UINT64_MAX) {
        /* handle 2^33 wrap-arounds */
        uint64_t delta = (TS_CLOCK_MAX + pts_orig -
                          (program->last_pcr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
        if (delta <= upipe_ts_demux_output->max_delay) {
            uint64_t pts = program->timestamp_offset +
                           program->last_pcr + delta;
            uref_clock_set_pts(uref, pts);
            if (pts > program->timestamp_highest)
                program->timestamp_highest = pts;
        } else
            upipe_warn_va(upipe, "too long delay for PTS (%"PRIu64")", delta);
    }
    if (dts_orig != UINT64_MAX) {
        /* handle 2^33 wrap-arounds */
        uint64_t delta = (TS_CLOCK_MAX + dts_orig -
                          (program->last_pcr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
        if (delta <= upipe_ts_demux_output->max_delay)
            uref_clock_set_dts(uref, program->timestamp_offset +
                                     program->last_pcr + delta);
        else
            upipe_warn_va(upipe, "too long delay for DTS (%"PRIu64")", delta);
    }

    upipe_throw(upipe, event, uref);
    return true;
}

/** @internal @This catches new_flow_def events coming from output subpipes.
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
        container_of(uprobe, struct upipe_ts_demux_output, probe);
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(
                upipe_ts_demux_program_to_upipe(program)->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, subpipe, event, args, &flow_def, &def))
        return false;

    if (ubase_ncmp(def, "block."))
        return false;

    if (!ubase_ncmp(def, "block.mpegts.")) {
        /* allocate ts_decaps subpipe */
        upipe_ts_demux_output->setrap =
            upipe_flow_alloc(ts_demux_mgr->setrap_mgr,
                             uprobe_pfx_adhoc_alloc(uprobe,
                                                    UPROBE_LOG_DEBUG, "setrap"),
                             flow_def);
        struct uref *flow_def2;
        if (unlikely(upipe_ts_demux_output->setrap == NULL ||
                     !upipe_setrap_set_rap(upipe_ts_demux_output->setrap,
                                           program->systime_pcr) ||
                     !upipe_get_flow_def(upipe_ts_demux_output->setrap,
                                         &flow_def2))) {
            if (upipe_ts_demux_output->setrap != NULL) {
                upipe_release(upipe_ts_demux_output->setrap);
                upipe_ts_demux_output->setrap = NULL;
            }
            upipe_throw_aerror(upipe);
            return true;
        }
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->ts_decaps_mgr,
                             uprobe_pfx_adhoc_alloc(uprobe,
                                                    UPROBE_LOG_DEBUG, "decaps"),
                             flow_def2);
        if (unlikely(output == NULL)) {
            upipe_release(upipe_ts_demux_output->setrap);
            upipe_throw_aerror(upipe);
        } else {
            upipe_set_output(upipe_ts_demux_output->setrap, output);
            upipe_set_output(subpipe, upipe_ts_demux_output->setrap);
            upipe_release(output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.mpegtspes.")) {
        /* allocate ts_pesd subpipe */
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->ts_pesd_mgr,
                             uprobe_pfx_adhoc_alloc(uprobe,
                                                    UPROBE_LOG_DEBUG, "pesd"),
                             flow_def);
        if (unlikely(output == NULL))
            upipe_throw_aerror(upipe);
        else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.mpeg2video.") &&
        ts_demux_mgr->mp2vf_mgr != NULL) {
        /* allocate mp2vf subpipe */
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->mp2vf_mgr,
                uprobe_pfx_adhoc_alloc(
                    &upipe_ts_demux_output->last_subpipe_probe,
                    UPROBE_LOG_DEBUG, "mp2vf"),
                flow_def);
        if (unlikely(output == NULL))
            upipe_throw_aerror(upipe);
        else {
            upipe_set_output(subpipe, output);
            upipe_ts_demux_output_store_last_subpipe(upipe, output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.h264.") &&
        ts_demux_mgr->h264f_mgr != NULL) {
        /* allocate mp2vf subpipe */
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->h264f_mgr,
                uprobe_pfx_adhoc_alloc(
                    &upipe_ts_demux_output->last_subpipe_probe,
                    UPROBE_LOG_DEBUG, "h264f"),
                flow_def);
        if (unlikely(output == NULL))
            upipe_throw_aerror(upipe);
        else {
            upipe_set_output(subpipe, output);
            upipe_ts_demux_output_store_last_subpipe(upipe, output);
        }
        return true;
    }

    return false;
}

/** @internal @This catches events coming from output subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_output
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_output_probe(struct uprobe *uprobe,
                                        struct upipe *subpipe,
                                        enum uprobe_event event,
                                        va_list args)
{
    switch (event) {
        case UPROBE_SYNC_ACQUIRED:
            /* we ignore this event not coming from a framer */
            return true;
        case UPROBE_SYNC_LOST:
            /* we ignore this event not coming from a framer */
            return true;
        case UPROBE_CLOCK_REF:
            return upipe_ts_demux_output_clock_ref(uprobe, subpipe, event,
                                                   args);
        case UPROBE_CLOCK_TS:
            return upipe_ts_demux_output_clock_ts(uprobe, subpipe, event, args);
        case UPROBE_NEW_FLOW_DEF:
            return upipe_ts_demux_output_plumber(uprobe, subpipe, event, args);
        default:
            return false;
    }
}

/** @internal @This allocates an output subpipe of a ts_demux_program subpipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_demux_output_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_demux_output_alloc_flow(mgr, uprobe,
                                                           signature,
                                                           args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    upipe_ts_demux_output_init_bin(upipe);
    upipe_ts_demux_output->pcr = false;
    upipe_ts_demux_output->split_output = NULL;
    upipe_ts_demux_output->setrap = NULL;
    upipe_ts_demux_output->max_delay = MAX_DELAY_STILL;
    uref_ts_flow_get_max_delay(flow_def, &upipe_ts_demux_output->max_delay);
    uprobe_init(&upipe_ts_demux_output->probe,
                upipe_ts_demux_output_probe, upipe->uprobe);

    upipe_ts_demux_output_init_sub(upipe);

    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);
    upipe_use(upipe_ts_demux_program_to_upipe(program));
    upipe_throw_ready(upipe);

    const char *def;
    if (unlikely(!uref_ts_flow_get_pid(flow_def, &upipe_ts_demux_output->pid) ||
                 upipe_ts_demux_output->pid >= MAX_PIDS ||
                 !uref_flow_get_raw_def(flow_def, &def) ||
                 !uref_flow_set_def(flow_def, def) ||
                 !uref_flow_delete_raw_def(flow_def))) {
        uref_free(flow_def);
        upipe_throw_aerror(upipe);
        return upipe;
    }

    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(
                upipe_ts_demux_program_to_upipe(program)->mgr);
    /* set up a split_output subpipe */
    upipe_ts_demux_output->split_output =
        upipe_flow_alloc_sub(demux->split,
                             uprobe_pfx_adhoc_alloc_va(
                                 &upipe_ts_demux_output->probe,
                                 UPROBE_LOG_DEBUG, "split output %"PRIu64,
                                 upipe_ts_demux_output->pid),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_output->split_output == NULL)) {
        upipe_throw_aerror(upipe);
        return upipe;
    }

    upipe_ts_demux_program_check_pcr(upipe_ts_demux_program_to_upipe(program));
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_output_free(struct upipe *upipe)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);

    if (upipe_ts_demux_output->split_output != NULL)
        upipe_release(upipe_ts_demux_output->split_output);
    if (upipe_ts_demux_output->setrap != NULL)
        upipe_release(upipe_ts_demux_output->setrap);
    upipe_throw_dead(upipe);

    upipe_ts_demux_output_clean_bin(upipe);
    upipe_ts_demux_output_clean_sub(upipe);
    upipe_ts_demux_output_free_flow(upipe);

    upipe_ts_demux_program_check_pcr(upipe_ts_demux_program_to_upipe(program));
    upipe_release(upipe_ts_demux_program_to_upipe(program));
}

/** @internal @This initializes the output manager for a ts_demux_program
 * subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_init_output_mgr(struct upipe *upipe)
{
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct upipe_mgr *output_mgr = &program->output_mgr;
    output_mgr->signature = UPIPE_TS_DEMUX_OUTPUT_SIGNATURE;
    output_mgr->upipe_alloc = upipe_ts_demux_output_alloc;
    output_mgr->upipe_input = NULL;
    output_mgr->upipe_control = upipe_ts_demux_output_control_bin;
    output_mgr->upipe_free = upipe_ts_demux_output_free;
    output_mgr->upipe_mgr_free = NULL;
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
    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(upipe->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, subpipe, event, args, &flow_def, &def))
        return false;

    if (!ubase_ncmp(def, "block.mpegtspsi.mpegtspmt.")) {
        /* allocate ts_pmtd subpipe */
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->ts_pmtd_mgr,
                             uprobe_pfx_adhoc_alloc(
                                 &upipe_ts_demux_program->pmtd_probe,
                                 UPROBE_LOG_DEBUG, "pmtd"),
                             flow_def);
        if (unlikely(output == NULL))
            upipe_throw_aerror(upipe);
        else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    return false;
}

/** @internal @This catches ts_pmtd_header events coming from pmtd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param pmtd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_program_pmtd_header(struct uprobe *uprobe,
                                               struct upipe *pmtd,
                                               enum uprobe_event event,
                                               va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pmtd_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    unsigned int signature = va_arg(args, unsigned int);
    assert(signature == UPIPE_TS_PMTD_SIGNATURE);
    struct uref *uref = va_arg(args, struct uref *);
    unsigned int pmtd_pcrpid = va_arg(args, unsigned int);
    unsigned int pmtd_desc_offset = va_arg(args, unsigned int);
    unsigned int pmtd_desc_size = va_arg(args, unsigned int);

    if (upipe_ts_demux_program->pcr_pid == pmtd_pcrpid)
        return true;

    if (upipe_ts_demux_program->pcr_split_output != NULL) {
        upipe_release(upipe_ts_demux_program->pcr_split_output);
        upipe_ts_demux_program->pcr_split_output = NULL;
    }

    upipe_ts_demux_program->pcr_pid = pmtd_pcrpid;
    upipe_ts_demux_program_check_pcr(upipe);

    /* send the event upstream, in case there is some descrambling involved */
    upipe_throw(upipe, event, signature, uref, pmtd_pcrpid,
                pmtd_desc_offset, pmtd_desc_size);
    return true;
}

/** @internal @This is a helper function to determine the maximum retention
 * delay of an h264 elementary stream.
 *
 * @param uref PMT table
 * @param pmtd_desc_offset offset of the ES descriptors in the uref
 * @param pmtd_desc_size size of the ES descriptors in the uref
 * @return max delay in 27 MHz time scale
 */
static uint64_t upipe_ts_demux_program_pmtd_h264_max_delay(struct uref *uref,
        unsigned int pmtd_desc_offset, unsigned int pmtd_desc_size)
{
    uint8_t buffer[pmtd_desc_size];
    bool still = true;
    const uint8_t *descl = uref_block_peek(uref, pmtd_desc_offset,
                                           pmtd_desc_size, buffer);
    const uint8_t *desc;
    int j = 0;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, pmtd_desc_size, j++))
            != NULL)
        if (desc_get_tag(desc) == 0x28 && desc28_validate(desc))
            break;

    if (desc != NULL)
        still = desc28_get_avc_still_present(desc);

    uref_block_peek_unmap(uref, pmtd_desc_offset, buffer, descl);

    return still ? MAX_DELAY_STILL : MAX_DELAY_14496;
}

/** @internal @This catches ts_pmtd_add_es events coming from pmtd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param pmtd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_program_pmtd_add_es(struct uprobe *uprobe,
                                               struct upipe *pmtd,
                                               enum uprobe_event event,
                                               va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pmtd_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

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
                uref_dup(upipe_ts_demux_program->flow_def_input);
            if (likely(flow_def != NULL &&
                       uref_flow_set_def(flow_def, "block.mpeg2video.pic.") &&
                       uref_flow_set_raw_def(flow_def,
                           "block.mpegts.mpegtspes.mpeg2video.pic.") &&
                       uref_ts_flow_set_pid(flow_def, pid) &&
                       uref_flow_set_program_va(flow_def, "%u,",
                           upipe_ts_demux_program->program) &&
                       uref_ts_flow_set_max_delay(flow_def, MAX_DELAY_STILL)))
                upipe_split_throw_add_flow(upipe, pid, flow_def);

            if (flow_def != NULL)
                uref_free(flow_def);
            break;
        }
        case 0x1b: {
            struct uref *flow_def =
                uref_dup(upipe_ts_demux_program->flow_def_input);
            if (likely(flow_def != NULL &&
                       uref_flow_set_def(flow_def, "block.h264.pic.") &&
                       uref_flow_set_raw_def(flow_def,
                           "block.mpegts.mpegtspes.h264.pic.") &&
                       uref_ts_flow_set_pid(flow_def, pid) &&
                       uref_flow_set_program_va(flow_def, "%u,",
                           upipe_ts_demux_program->program) &&
                       uref_ts_flow_set_max_delay(flow_def,
                           upipe_ts_demux_program_pmtd_h264_max_delay(uref,
                               pmtd_desc_offset, pmtd_desc_size))))
                upipe_split_throw_add_flow(upipe, pid, flow_def);

            if (flow_def != NULL)
                uref_free(flow_def);
            break;
        }
        default:
            upipe_warn_va(upipe, "unhandled stream type %u for PID %u",
                          streamtype, pid);
            break;
    }

    /* send the event upstream, in case there is some descrambling involved */
    upipe_throw(upipe, event, signature, uref, pid, streamtype,
                pmtd_desc_offset, pmtd_desc_size);
    return true;
}

/** @internal @This catches ts_pmtd_del_es events coming from pmtd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param pmtd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_program_pmtd_del_es(struct uprobe *uprobe,
                                               struct upipe *pmtd,
                                               enum uprobe_event event,
                                               va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pmtd_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    unsigned int signature = va_arg(args, unsigned int);
    assert(signature == UPIPE_TS_PMTD_SIGNATURE);
    struct uref *uref = va_arg(args, struct uref *);
    unsigned int pid = va_arg(args, unsigned int);

    upipe_split_throw_del_flow(upipe, pid);

    /* send read_end on the output */
    struct uchain *uchain;
    struct upipe_ts_demux_output *output = NULL;
    ulist_foreach (&upipe_ts_demux_program->outputs, uchain) {
        if (output != NULL)
            upipe_release(upipe_ts_demux_output_to_upipe(output));
        output = upipe_ts_demux_output_from_uchain(uchain);
        /* to avoid having the uchain disappear during upipe_throw_read_end */
        upipe_use(upipe_ts_demux_output_to_upipe(output));
        if (output->pid == pid)
            upipe_throw_source_end(upipe_ts_demux_output_to_upipe(output));
    }
    if (output != NULL)
        upipe_release(upipe_ts_demux_output_to_upipe(output));

    /* send the event upstream, in case there is some descrambling involved */
    upipe_throw(upipe, event, signature, uref, pid);
    return true;
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

    switch (event) {
        case UPROBE_TS_PMTD_SYSTIME: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PMTD_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            uint64_t systime;
            if (uref_clock_get_systime_rap(uref, &systime))
                upipe_ts_demux_program->systime_pmt = systime;
            upipe_throw(upipe, event, signature, uref);
            return true;
        }
        case UPROBE_TS_PMTD_HEADER:
            return upipe_ts_demux_program_pmtd_header(uprobe, pmtd, event,
                                                      args);
        case UPROBE_TS_PMTD_ADD_ES:
            return upipe_ts_demux_program_pmtd_add_es(uprobe, pmtd, event,
                                                      args);
        case UPROBE_TS_PMTD_DEL_ES:
            return upipe_ts_demux_program_pmtd_del_es(uprobe, pmtd, event,
                                                      args);
        default:
            return false;
    }
}

/** @internal @This catches events coming from PCR ts_decaps subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param pmtd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_program_pcr_probe(struct uprobe *uprobe,
                                             struct upipe *pmtd,
                                             enum uprobe_event event,
                                             va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pcr_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    if (event != UPROBE_CLOCK_REF)
        return false;

    struct uref *uref = va_arg(args, struct uref *);
    uint64_t pcr_orig = va_arg(args, uint64_t);
    int discontinuity = va_arg(args, int);
    upipe_ts_demux_program_handle_pcr(upipe, uref, pcr_orig, discontinuity);
    return true;
}

/** @internal @This handles PCRs coming from clock_ref events.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the PCR
 * @param pcr_orig PCR value
 * @param discontinuity true if a discontinuity occurred before
 */
static void upipe_ts_demux_program_handle_pcr(struct upipe *upipe,
                                              struct uref *uref,
                                              uint64_t pcr_orig,
                                              int discontinuity)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);

    /* handle 2^33 wrap-arounds */
    uint64_t delta =
        (TS_CLOCK_MAX + pcr_orig -
         (upipe_ts_demux_program->last_pcr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
    if (delta <= MAX_PCR_INTERVAL && !discontinuity)
        upipe_ts_demux_program->last_pcr += delta;
    else {
        upipe_warn(upipe, "PCR discontinuity");
        upipe_ts_demux_program->last_pcr = pcr_orig;
        upipe_ts_demux_program->timestamp_offset =
            upipe_ts_demux_program->timestamp_highest - pcr_orig;
    }
    upipe_throw_clock_ref(upipe, uref,
                          upipe_ts_demux_program->last_pcr +
                          upipe_ts_demux_program->timestamp_offset,
                          discontinuity);

    if (upipe_ts_demux_program->systime_pmt) {
        bool ret = true;
        struct uchain *uchain;
        struct upipe_ts_demux_output *output = NULL;
        upipe_ts_demux_program->systime_pcr =
            upipe_ts_demux_program->systime_pmt;
        ulist_foreach (&upipe_ts_demux_program->outputs, uchain) {
            output = upipe_ts_demux_output_from_uchain(uchain);
            if (output->setrap != NULL)
                ret = ret && upipe_setrap_set_rap(output->setrap,
                                    upipe_ts_demux_program->systime_pcr);
        }
        /* this is also valid for the packet we are processing */
        uref_clock_set_systime_rap(uref, upipe_ts_demux_program->systime_pcr);
        if (!ret)
            upipe_throw_aerror(upipe);
    }
}

/** @internal @This checks whether there is a ts_decaps on the PID
 * carrying the PCR, and otherwise allocates/deallocates one.
 *
 * @param upipe description structure of the pipe
 * @return true if there is already a ts_decaps on the PCR PID
 */
static void upipe_ts_demux_program_check_pcr(struct upipe *upipe)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(upipe->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);
    bool found = upipe_ts_demux_program->pcr_pid == 8191;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_demux_program->outputs, uchain) {
        struct upipe_ts_demux_output *output =
            upipe_ts_demux_output_from_uchain(uchain);
        if (output->pid == upipe_ts_demux_program->pcr_pid) {
            output->pcr = !found;
            found = true;
        } else
            output->pcr = false;
    }

    if (found) {
        if (upipe_ts_demux_program->pcr_split_output != NULL) {
            upipe_release(upipe_ts_demux_program->pcr_split_output);
            upipe_ts_demux_program->pcr_split_output = NULL;
        }
        return;
    }
    if (upipe_ts_demux_program->pcr_split_output != NULL)
        return;

    struct uref *flow_def = uref_dup(upipe_ts_demux_program->flow_def_input);
    if (unlikely(flow_def == NULL ||
                 !uref_flow_set_def(flow_def, "block.mpegts.") ||
                 !uref_ts_flow_set_pid(flow_def,
                                       upipe_ts_demux_program->pcr_pid) ||
                 !uref_flow_set_program_va(flow_def, "%u,",
                                           upipe_ts_demux_program->program))) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_aerror(upipe);
        return;
    }
    upipe_ts_demux_program->pcr_split_output =
        upipe_flow_alloc_sub(demux->split,
                             uprobe_pfx_adhoc_alloc_va(
                                 upipe_ts_demux_to_upipe(demux)->uprobe,
                                 UPROBE_LOG_DEBUG, "split output PCR %"PRIu64,
                                 upipe_ts_demux_program->pcr_pid),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_program->pcr_split_output == NULL ||
                 !upipe_get_flow_def(upipe_ts_demux_program->pcr_split_output,
                                     &flow_def))) {
        upipe_throw_aerror(upipe);
        return;
    }

    struct upipe *decaps =
        upipe_flow_alloc(ts_demux_mgr->ts_decaps_mgr,
                         uprobe_pfx_adhoc_alloc_va(
                             &upipe_ts_demux_program->pcr_probe,
                             UPROBE_LOG_DEBUG,
                             "decaps PCR %"PRIu64,
                             upipe_ts_demux_program->pcr_pid),
                         flow_def);
    if (unlikely(decaps == NULL ||
                 !upipe_set_output(decaps, demux->null) ||
                 !upipe_set_output(upipe_ts_demux_program->pcr_split_output,
                                   decaps))) {
        if (decaps != NULL)
            upipe_release(decaps);
        upipe_release(upipe_ts_demux_program->pcr_split_output);
        upipe_ts_demux_program->pcr_split_output = NULL;
        upipe_throw_aerror(upipe);
        return;
    }
    upipe_release(decaps);
}

/** @internal @This allocates a program subpipe of a ts_demux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_demux_program_alloc(struct upipe_mgr *mgr,
                                                  struct uprobe *uprobe,
                                                  uint32_t signature,
                                                  va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_demux_program_alloc_flow(mgr, uprobe,
                                                            signature,
                                                            args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    upipe_ts_demux_program_init_output_mgr(upipe);
    upipe_ts_demux_program_init_sub_outputs(upipe);
    upipe_ts_demux_program->flow_def_input = flow_def;
    upipe_ts_demux_program->program = 0;
    upipe_ts_demux_program->systime_pmt = 0;
    upipe_ts_demux_program->systime_pcr = 0;
    upipe_ts_demux_program->pcr_pid = 0;
    upipe_ts_demux_program->pcr_split_output = NULL;
    upipe_ts_demux_program->psi_split_output = NULL;
    upipe_ts_demux_program->timestamp_offset = 0;
    upipe_ts_demux_program->timestamp_highest = TS_CLOCK_MAX;
    upipe_ts_demux_program->last_pcr = TS_CLOCK_MAX;
    uprobe_init(&upipe_ts_demux_program->plumber,
                upipe_ts_demux_program_plumber, upipe->uprobe);
    uprobe_init(&upipe_ts_demux_program->pmtd_probe,
                upipe_ts_demux_program_pmtd_probe, upipe->uprobe);
    uprobe_init(&upipe_ts_demux_program->pcr_probe,
                upipe_ts_demux_program_pcr_probe, upipe->uprobe);

    upipe_ts_demux_program_init_sub(upipe);

    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(upipe->mgr);
    upipe_use(upipe_ts_demux_to_upipe(demux));
    upipe_throw_ready(upipe);

    uint64_t pid;
    const char *program;
    const uint8_t *filter, *mask;
    size_t size;
    const char *def;
    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL ||
                 !uref_ts_flow_get_pid(flow_def, &pid) || pid >= MAX_PIDS ||
                 !uref_ts_flow_get_psi_filter(flow_def, &filter, &mask,
                                              &size) ||
                 !uref_flow_get_program(flow_def, &program) ||
                 sscanf(program, "%u,",
                        &upipe_ts_demux_program->program) != 1 ||
                 upipe_ts_demux_program->program == 0 ||
                 upipe_ts_demux_program->program > UINT16_MAX ||
                 !uref_flow_get_raw_def(flow_def, &def) ||
                 !uref_flow_set_def(flow_def, def) ||
                 !uref_flow_delete_raw_def(flow_def) ||
                 (upipe_ts_demux_program->psi_pid =
                      upipe_ts_demux_psi_pid_use(upipe_ts_demux_to_upipe(demux),
                                                 pid)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_aerror(upipe);
        return upipe;
    }

    upipe_ts_demux_program->psi_split_output =
        upipe_flow_alloc_sub(upipe_ts_demux_program->psi_pid->psi_split,
                             uprobe_pfx_adhoc_alloc(
                                 &upipe_ts_demux_program->plumber,
                                 UPROBE_LOG_DEBUG, "psi_split output"),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_program->psi_split_output == NULL)) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
        upipe_ts_demux_program->psi_pid = NULL;
        upipe_throw_aerror(upipe);
        return upipe;
    }

    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_free(struct upipe *upipe)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct upipe_ts_demux *demux =
        upipe_ts_demux_from_program_mgr(upipe->mgr);

    if (upipe_ts_demux_program->psi_split_output != NULL) {
        upipe_release(upipe_ts_demux_program->psi_split_output);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
    }
    if (upipe_ts_demux_program->pcr_split_output != NULL)
        upipe_release(upipe_ts_demux_program->pcr_split_output);
    upipe_throw_dead(upipe);

    uref_free(upipe_ts_demux_program->flow_def_input);
    upipe_ts_demux_program_clean_sub_outputs(upipe);
    upipe_ts_demux_program_clean_sub(upipe);
    upipe_ts_demux_program_free_flow(upipe);

    upipe_release(upipe_ts_demux_to_upipe(demux));
}

/** @This is called when the proxy is released.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_proxy_released(struct upipe *upipe)
{
    upipe_ts_demux_program_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
}

/** @internal @This initializes the output manager for a ts_demux pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_demux->program_mgr;
    program_mgr->signature = UPIPE_TS_DEMUX_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_demux_program_alloc;
    program_mgr->upipe_input = NULL;
    program_mgr->upipe_control = NULL;
    program_mgr->upipe_free = upipe_ts_demux_program_free;
    program_mgr->upipe_mgr_free = NULL;
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
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->ts_decaps_mgr,
                             uprobe_pfx_adhoc_alloc(uprobe,
                                                    UPROBE_LOG_DEBUG, "decaps"),
                             flow_def);
        if (unlikely(output == NULL))
            upipe_throw_aerror(upipe);
        else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    if (!ubase_ncmp(def, "block.mpegtspsi.")) {
        /* allocate ts_psim subpipe */
        struct upipe *output =
            upipe_flow_alloc(ts_demux_mgr->ts_psim_mgr,
                             uprobe_pfx_adhoc_alloc(&upipe_ts_demux->psim_probe,
                                                    UPROBE_LOG_DEBUG, "psim"),
                             flow_def);
        if (unlikely(output == NULL))
            upipe_throw_aerror(upipe);
        else {
            upipe_set_output(subpipe, output);
            upipe_release(output);
        }
        return true;
    }

    return false;
}

/** @internal @This catches the new_flow_def events coming from psim subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param upipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_psim_probe(struct uprobe *uprobe,
                                      struct upipe *psim,
                                      enum uprobe_event event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, psim_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
            /* we catch the event because we have no way to send it upstream */
            return true;
        case UPROBE_NEW_FLOW_DEF:
            break;
        default:
            return false;
    }

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(uprobe, psim, event, args, &flow_def, &def))
        return false;

    uint64_t pid;
    if (unlikely(!uref_ts_flow_get_pid(flow_def, &pid))) {
        upipe_warn(upipe, "invalid flow definition");
        return true;
    }

    struct upipe_ts_demux_psi_pid *psi_pid =
        upipe_ts_demux_psi_pid_find(upipe, pid);
    if (unlikely(psi_pid == NULL)) {
        upipe_warn_va(upipe, "unknown PSI PID %"PRIu64, pid);
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

/** @internal @This catches ts_patd_systime events coming from patd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param patd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_patd_systime(struct uprobe *uprobe,
                                        struct upipe *patd,
                                        enum uprobe_event event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, patd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    unsigned int signature = va_arg(args, unsigned int);
    struct uref *uref = va_arg(args, struct uref *);
    uint64_t systime = va_arg(args, uint64_t);
    assert(signature == UPIPE_TS_PATD_SIGNATURE);

    if (unlikely(!upipe_setrap_set_rap(upipe_ts_demux->setrap, systime)))
        upipe_throw_aerror(upipe);

    upipe_throw(upipe, event, signature, uref, systime);
    return true;
}

/** @internal @This catches ts_patd_add_program events coming from patd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param patd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_patd_add_program(struct uprobe *uprobe,
                                            struct upipe *patd,
                                            enum uprobe_event event,
                                            va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, patd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

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

    struct uref *flow_def = uref_dup(upipe_ts_demux->flow_def_input);
    if (likely(flow_def != NULL &&
               uref_flow_set_def(flow_def, "program.") &&
               uref_flow_set_raw_def(flow_def, "block.mpegtspsi.mpegtspmt.") &&
               uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                           PSI_HEADER_SIZE_SYNTAX1) &&
               uref_ts_flow_set_pid(flow_def, pid) &&
               uref_flow_set_program_va(flow_def, "%u,", program)))
        upipe_split_throw_add_flow(upipe, program, flow_def);

    if (flow_def != NULL)
        uref_free(flow_def);

    /* send the event upstream, in case there is some descrambling involved */
    upipe_throw(upipe, event, signature, uref, program, pid);
    return true;
}

/** @internal @This catches ts_patd_del_program events coming from patd subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param patd pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_patd_del_program(struct uprobe *uprobe,
                                            struct upipe *patd,
                                            enum uprobe_event event,
                                            va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, patd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    unsigned int signature = va_arg(args, unsigned int);
    struct uref *uref = va_arg(args, struct uref *);
    unsigned int pmtd_program = va_arg(args, unsigned int);
    assert(signature == UPIPE_TS_PATD_SIGNATURE);

    upipe_split_throw_del_flow(upipe, pmtd_program);

    /* send read_end on the program */
    struct uchain *uchain;
    struct upipe_ts_demux_program *program = NULL;
    ulist_foreach (&upipe_ts_demux->programs, uchain) {
        if (program != NULL)
            upipe_release(upipe_ts_demux_program_to_upipe(program));
        program = upipe_ts_demux_program_from_uchain(uchain);
        /* to avoid having the uchain disappear during upipe_throw_read_end */
        upipe_use(upipe_ts_demux_program_to_upipe(program));
        if (program->program == pmtd_program)
            upipe_throw_source_end(upipe_ts_demux_program_to_upipe(program));
    }
    if (program != NULL)
        upipe_release(upipe_ts_demux_program_to_upipe(program));

    /* send the event upstream, in case there is some descrambling involved */
    upipe_throw(upipe, event, signature, uref, pmtd_program);
    return true;
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
        case UPROBE_TS_PATD_SYSTIME:
            return upipe_ts_demux_patd_systime(uprobe, patd, event, args);
        case UPROBE_TS_PATD_TSID:
            upipe_throw_va(upipe, event, args);
            return true;
        case UPROBE_TS_PATD_ADD_PROGRAM:
            return upipe_ts_demux_patd_add_program(uprobe, patd, event, args);
        case UPROBE_TS_PATD_DEL_PROGRAM:
            return upipe_ts_demux_patd_del_program(uprobe, patd, event, args);
        default:
            return false;
    }
}

/** @internal @This catches events coming from sync or check subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param subpipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_input_probe(struct uprobe *uprobe,
                                       struct upipe *subpipe,
                                       enum uprobe_event event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, input_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_SYNC_ACQUIRED:
            upipe_ts_demux_sync_acquired(upipe);
            return true;
        case UPROBE_SYNC_LOST:
            upipe_ts_demux_sync_lost(upipe);
            return true;
        default:
            return false;
    }
}

/** @internal @This catches events coming from split subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param subpipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_demux_split_probe(struct uprobe *uprobe,
                                       struct upipe *subpipe,
                                       enum uprobe_event event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, split_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_TS_SPLIT_ADD_PID:
        case UPROBE_TS_SPLIT_DEL_PID:
            upipe_throw_va(upipe, event, args);
            return true;
        default:
            return false;
    }
}

/** @internal @This allocates a ts_demux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_demux_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(mgr);
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_demux_alloc_flow(mgr, uprobe, signature,
                                                    args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;
    const char *def;
    uref_flow_get_def(flow_def, &def);
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    upipe_ts_demux_init_program_mgr(upipe);
    upipe_ts_demux_init_sub_programs(upipe);
    upipe->sub_mgr = upipe_proxy_mgr_alloc(upipe->sub_mgr,
                                     upipe_ts_demux_program_proxy_released);
    if (unlikely(upipe->sub_mgr == NULL)) {
        upipe_ts_demux_clean_sub_programs(upipe);
        uref_free(flow_def);
        free(upipe_ts_demux);
        return NULL;
    }

    upipe_ts_demux_init_sync(upipe);
    upipe_ts_demux->flow_def_input = flow_def;
    upipe_ts_demux->input = upipe_ts_demux->split = upipe_ts_demux->setrap =
        upipe_ts_demux->null = upipe_ts_demux->psi_split_output_pat = NULL;
    upipe_ts_demux->psi_pid_pat = NULL;

    ulist_init(&upipe_ts_demux->psi_pids);
    upipe_ts_demux->conformance = CONFORMANCE_ISO;
    upipe_ts_demux->auto_conformance = true;
    upipe_ts_demux->nit_pid = 0;

    uprobe_init(&upipe_ts_demux->psi_pid_plumber,
                upipe_ts_demux_psi_pid_plumber, upipe->uprobe);
    uprobe_init(&upipe_ts_demux->psim_probe, upipe_ts_demux_psim_probe,
                upipe->uprobe);
    uprobe_init(&upipe_ts_demux->patd_probe,
                upipe_ts_demux_patd_probe, upipe->uprobe);
    uprobe_init(&upipe_ts_demux->input_probe,
                upipe_ts_demux_input_probe, upipe->uprobe);
    uprobe_init(&upipe_ts_demux->split_probe,
                upipe_ts_demux_split_probe, upipe->uprobe);

    upipe_throw_ready(upipe);
    upipe_ts_demux_sync_lost(upipe);

    if (ubase_ncmp(def, EXPECTED_FLOW_DEF_SYNC)) {
        if (!ubase_ncmp(def, EXPECTED_FLOW_DEF_CHECK))
            /* allocate ts_check subpipe */
            upipe_ts_demux->input =
                upipe_flow_alloc(ts_demux_mgr->ts_check_mgr,
                                 uprobe_pfx_adhoc_alloc(upipe->uprobe,
                                                        UPROBE_LOG_DEBUG,
                                                        "check"),
                                 flow_def);
        else
            /* allocate ts_sync subpipe */
            upipe_ts_demux->input =
                upipe_flow_alloc(ts_demux_mgr->ts_sync_mgr,
                                 uprobe_pfx_adhoc_alloc(upipe->uprobe,
                                                        UPROBE_LOG_DEBUG,
                                                        "sync"),
                                 flow_def);
        if (unlikely(upipe_ts_demux->input == NULL)) {
            upipe_throw_aerror(upipe);
            return upipe;
        }
        upipe_get_flow_def(upipe_ts_demux->input, &flow_def);
    }

    upipe_ts_demux->setrap =
        upipe_flow_alloc(ts_demux_mgr->setrap_mgr,
                         uprobe_pfx_adhoc_alloc(upipe->uprobe,
                                                UPROBE_LOG_DEBUG, "setrap"),
                         flow_def);
    if (unlikely(upipe_ts_demux->setrap == NULL)) {
        upipe_throw_aerror(upipe);
        return upipe;
    }

    if (upipe_ts_demux->input == NULL) {
        upipe_ts_demux->input = upipe_ts_demux->setrap;
        upipe_use(upipe_ts_demux->setrap);
        upipe_ts_demux_sync_acquired(upipe);
        upipe_get_flow_def(upipe_ts_demux->setrap, &flow_def);
    } else
        upipe_set_output(upipe_ts_demux->input, upipe_ts_demux->setrap);

    upipe_ts_demux->split =
        upipe_flow_alloc(ts_demux_mgr->ts_split_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_demux->split_probe,
                                                UPROBE_LOG_DEBUG, "split"),
                         flow_def);
    if (unlikely(upipe_ts_demux->split == NULL)) {
        upipe_throw_aerror(upipe);
        return upipe;
    }
    upipe_set_output(upipe_ts_demux->setrap, upipe_ts_demux->split);

    upipe_ts_demux->null =
        upipe_flow_alloc(ts_demux_mgr->null_mgr,
                         uprobe_pfx_adhoc_alloc(upipe->uprobe,
                                                UPROBE_LOG_NOTICE, "null"),
                         NULL);
    if (unlikely(upipe_ts_demux->null == NULL)) {
        upipe_throw_aerror(upipe);
        return upipe;
    }

    /* get psi_split subpipe */
    upipe_ts_demux->psi_pid_pat = upipe_ts_demux_psi_pid_use(upipe, 0);
    if (unlikely(upipe_ts_demux->psi_pid_pat == NULL)) {
        upipe_throw_aerror(upipe);
        return upipe;
    }

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
    flow_def = uref_dup(upipe_ts_demux->flow_def_input);
    if (unlikely(flow_def == NULL ||
                 !uref_flow_set_def(flow_def, "block.mpegtspsi.mpegtspat.") ||
                 !uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE_SYNTAX1) ||
                 !uref_ts_flow_set_pid(flow_def, 0) ||
                 (upipe_ts_demux->psi_split_output_pat =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux->psi_pid_pat->psi_split,
                          uprobe_pfx_adhoc_alloc(upipe->uprobe,
                              UPROBE_LOG_DEBUG, "psi_split output"),
                          flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_aerror(upipe);
        return upipe;
    }
    uref_free(flow_def);
    upipe_get_flow_def(upipe_ts_demux->psi_split_output_pat, &flow_def);

    /* allocate PAT decoder */
    struct upipe *patd =
        upipe_flow_alloc(ts_demux_mgr->ts_patd_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_demux->patd_probe,
                                                UPROBE_LOG_DEBUG, "patd"),
                         flow_def);
    if (unlikely(patd == NULL)) {
        upipe_throw_aerror(upipe);
        return upipe;
    }
    upipe_set_output(upipe_ts_demux->psi_split_output_pat, patd);
    upipe_release(patd);
    return upipe;
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
    upipe_input(upipe_ts_demux->input, uref, upump);
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

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_free(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->input != NULL)
        upipe_release(upipe_ts_demux->input);
    if (upipe_ts_demux->split != NULL)
        upipe_release(upipe_ts_demux->split);
    if (upipe_ts_demux->setrap != NULL)
        upipe_release(upipe_ts_demux->setrap);
    if (upipe_ts_demux->null != NULL)
        upipe_release(upipe_ts_demux->null);
    if (upipe_ts_demux->psi_split_output_pat != NULL)
        upipe_release(upipe_ts_demux->psi_split_output_pat);
    if (upipe_ts_demux->psi_pid_pat != NULL)
        upipe_ts_demux_psi_pid_release(upipe, upipe_ts_demux->psi_pid_pat);
    struct upipe_mgr *meuh;
    meuh = upipe_proxy_mgr_get_super_mgr(upipe->sub_mgr);
    upipe_throw_dead(upipe);
    upipe_mgr_release(upipe->sub_mgr);
    uref_free(upipe_ts_demux->flow_def_input);
    upipe_ts_demux_clean_sub_programs(upipe);
    upipe_ts_demux_clean_sync(upipe);
    upipe_ts_demux_free_flow(upipe);
}

/** @This is called when the proxy is released.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_proxy_released(struct upipe *upipe)
{
    upipe_ts_demux_throw_sub_programs(upipe, UPROBE_SOURCE_END);
}

/** @This frees a upipe manager.
 *
 * @param mgr pointer to manager
 */
static void upipe_ts_demux_mgr_free(struct upipe_mgr *mgr)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(mgr);
    if (ts_demux_mgr->null_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->null_mgr);
    if (ts_demux_mgr->setrap_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->setrap_mgr);
    if (ts_demux_mgr->ts_split_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_split_mgr);
    if (ts_demux_mgr->ts_sync_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_sync_mgr);
    if (ts_demux_mgr->ts_check_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_check_mgr);
    if (ts_demux_mgr->ts_decaps_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_decaps_mgr);
    if (ts_demux_mgr->ts_psim_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_psim_mgr);
    if (ts_demux_mgr->ts_psi_split_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_psi_split_mgr);
    if (ts_demux_mgr->ts_patd_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_patd_mgr);
    if (ts_demux_mgr->ts_pmtd_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_pmtd_mgr);
    if (ts_demux_mgr->ts_pesd_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->ts_pesd_mgr);
    if (ts_demux_mgr->mp2vf_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->mp2vf_mgr);
    if (ts_demux_mgr->h264f_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->h264f_mgr);

    urefcount_clean(&ts_demux_mgr->mgr.refcount);
    free(ts_demux_mgr);
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

    ts_demux_mgr->null_mgr = upipe_null_mgr_alloc();
    ts_demux_mgr->setrap_mgr = upipe_setrap_mgr_alloc();

    ts_demux_mgr->ts_split_mgr = upipe_ts_split_mgr_alloc();
    ts_demux_mgr->ts_sync_mgr = upipe_ts_sync_mgr_alloc();
    ts_demux_mgr->ts_check_mgr = upipe_ts_check_mgr_alloc();
    ts_demux_mgr->ts_decaps_mgr = upipe_ts_decaps_mgr_alloc();
    ts_demux_mgr->ts_psim_mgr = upipe_ts_psim_mgr_alloc();
    ts_demux_mgr->ts_psi_split_mgr = upipe_ts_psi_split_mgr_alloc();
    ts_demux_mgr->ts_patd_mgr = upipe_ts_patd_mgr_alloc();
    ts_demux_mgr->ts_pmtd_mgr = upipe_ts_pmtd_mgr_alloc();
    ts_demux_mgr->ts_pesd_mgr = upipe_ts_pesd_mgr_alloc();

    ts_demux_mgr->mp2vf_mgr = NULL;
    ts_demux_mgr->h264f_mgr = NULL;

    ts_demux_mgr->mgr.signature = UPIPE_TS_DEMUX_SIGNATURE;
    ts_demux_mgr->mgr.upipe_alloc = upipe_ts_demux_alloc;
    ts_demux_mgr->mgr.upipe_input = upipe_ts_demux_input;
    ts_demux_mgr->mgr.upipe_control = upipe_ts_demux_control;
    ts_demux_mgr->mgr.upipe_free = upipe_ts_demux_free;
    ts_demux_mgr->mgr.upipe_mgr_free = upipe_ts_demux_mgr_free;
    urefcount_init(&ts_demux_mgr->mgr.refcount);
    return upipe_proxy_mgr_alloc(upipe_ts_demux_mgr_to_upipe_mgr(ts_demux_mgr),
                                 upipe_ts_demux_proxy_released);
}

/** @This processes control commands on a ts_demux manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_demux_mgr_control_va(struct upipe_mgr *mgr,
                                   enum upipe_ts_demux_mgr_command command,
                                   va_list args)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_proxy_mgr_get_super_mgr(mgr));
    assert(urefcount_single(&ts_demux_mgr->mgr.refcount));

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_TS_DEMUX_MGR_GET_##NAME##_MGR: {                         \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = ts_demux_mgr->name##_mgr;                                  \
            return true;                                                    \
        }                                                                   \
        case UPIPE_TS_DEMUX_MGR_SET_##NAME##_MGR: {                         \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            if (ts_demux_mgr->name##_mgr != NULL)                           \
                upipe_mgr_release(ts_demux_mgr->name##_mgr);                \
            if (m != NULL)                                                  \
                upipe_mgr_use(m);                                           \
            ts_demux_mgr->name##_mgr = m;                                   \
            return true;                                                    \
        }

        GET_SET_MGR(ts_split, TS_SPLIT)
        GET_SET_MGR(ts_sync, TS_SYNC)
        GET_SET_MGR(ts_check, TS_CHECK)
        GET_SET_MGR(ts_decaps, TS_DECAPS)
        GET_SET_MGR(ts_psim, TS_PSIM)
        GET_SET_MGR(ts_psi_split, TS_PSI_SPLIT)
        GET_SET_MGR(ts_patd, TS_PATD)
        GET_SET_MGR(ts_pmtd, TS_PMTD)
        GET_SET_MGR(ts_pesd, TS_PESD)

        GET_SET_MGR(mp2vf, MP2VF)
        GET_SET_MGR(h264f, H264F)
#undef GET_SET_MGR

        default:
            return false;
    }
}

/** @This processes control commands on a ts_demux manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_demux_mgr_control(struct upipe_mgr *mgr,
                                enum upipe_ts_demux_mgr_command command, ...)
{
    va_list args;
    va_start(args, command);
    bool ret = upipe_ts_demux_mgr_control_va(mgr, command, args);
    va_end(args);
    return ret;
}

/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * framer inner pipes
 * @item program split pipe, which is returned to the application, and
 * represents a program; it sets up the ts_split_output and ts_pmtd inner pipes
 * @item demux sink pipe which sets up the ts_split, ts_patd and optional input
 * synchronizer inner pipes
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_setrap.h>
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
#define POW2_33 UINT64_C(8589934592)
/** max resolution of PCR, PTS and DTS */
#define TS_CLOCK_MAX (POW2_33 * UCLOCK_FREQ / 90000)
/** max interval between PCRs (ISO/IEC 13818-1 2.7.2) - could be 100 ms but
 * allow higher tolerance */
#define MAX_PCR_INTERVAL (UCLOCK_FREQ / 2)
/** max retention time for most streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY UCLOCK_FREQ
/** max retention time for ISO/IEC 14496 streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_14496 (UCLOCK_FREQ * 10)
/** max retention time for still pictures streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_STILL (UCLOCK_FREQ * 60)

/** @internal @This is the private context of a ts_demux manager. */
struct upipe_ts_demux_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

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
    /** pointer to mpgaf manager */
    struct upipe_mgr *mpgaf_mgr;
    /** pointer to a52f manager */
    struct upipe_mgr *a52f_mgr;
    /** pointer to mpgvf manager */
    struct upipe_mgr *mpgvf_mgr;
    /** pointer to h264f manager */
    struct upipe_mgr *h264f_mgr;
    /** pointer to telxf manager */
    struct upipe_mgr *telxf_mgr;
    /** pointer to dvbsubf manager */
    struct upipe_mgr *dvbsubf_mgr;
    /** pointer to opusf manager */
    struct upipe_mgr *opusf_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_ts_demux_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_ts_demux_mgr, urefcount, urefcount, urefcount)

/** @hidden */
struct upipe_ts_demux_psi_pid;

/** @internal @This is the private context of a ts_demux pipe. */
struct upipe_ts_demux {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;
    /** pointer to the last inner pipe */
    struct upipe *last_inner;
    /** pointer to the output of the last inner pipe */
    struct upipe *output;

    /** pointer to null inner pipe */
    struct upipe *null;

    /** pointer to input inner pipe */
    struct upipe *input;
    /** true if we have thrown the sync_acquired event */
    bool acquired;
    /** flow definition of the input */
    struct uref *flow_def_input;

    /** pointer to setrap inner pipe */
    struct upipe *setrap;
    /** pointer to ts_split inner pipe */
    struct upipe *split;
    /** psi_pid structure for PAT */
    struct upipe_ts_demux_psi_pid *psi_pid_pat;
    /** ts_psi_split_output inner pipe for PAT */
    struct upipe *psi_split_output_pat;

    /** list of PIDs carrying PSI */
    struct uchain psi_pids;
    /** PID of the NIT */
    uint64_t nit_pid;
    /** true if the conformance is guessed from the stream */
    bool auto_conformance;
    /** current conformance */
    enum upipe_ts_conformance conformance;

    /** probe to get new flow events from inner pipes created by psi_pid
     * objects */
    struct uprobe psi_pid_plumber;
    /** probe to get events from ts_psim inner pipes created by psi_pid
     * objects */
    struct uprobe psim_probe;
    /** probe to get events from ts_patd inner pipe */
    struct uprobe patd_probe;
    /** probe to get events from ts_sync or ts_check inner pipe */
    struct uprobe input_probe;
    /** probe to get events from ts_split inner pipe */
    struct uprobe split_probe;
    /** probe to proxify events from other pipes */
    struct uprobe proxy_probe;

    /** list of programs */
    struct uchain programs;

    /** manager to create programs */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux, upipe, UPIPE_TS_DEMUX_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_demux, urefcount, upipe_ts_demux_no_input)
UPIPE_HELPER_VOID(upipe_ts_demux)
UPIPE_HELPER_SYNC(upipe_ts_demux, acquired)
UPIPE_HELPER_BIN_INPUT(upipe_ts_demux, input, input_request_list)
UPIPE_HELPER_BIN_OUTPUT(upipe_ts_demux, last_inner_probe, last_inner, output,
                        output_request_list)

UBASE_FROM_TO(upipe_ts_demux, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ts_demux_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of a program of a ts_demux pipe. */
struct upipe_ts_demux_program {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** list of output bin requests */
    struct uchain output_request_list;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;
    /** pointer to the last inner pipe */
    struct upipe *last_inner;
    /** pointer to the output of the last inner pipe */
    struct upipe *output;

    /** flow definition of the input */
    struct uref *flow_def_input;
    /** program number */
    uint64_t program;
    /** ts_psi_split_output inner pipe */
    struct upipe *psi_split_output;
    /** pointer to psi_pid structure */
    struct upipe_ts_demux_psi_pid *psi_pid;
    /** systime_rap of the last PMT */
    uint64_t pmt_rap;

    /** PMT PID */
    uint64_t pmt_pid;
    /** PCR PID */
    uint16_t pcr_pid;
    /** PCR ts_split output inner pipe */
    struct upipe *pcr_split_output;

    /** offset between MPEG timestamps and Upipe timestamps */
    int64_t timestamp_offset;
    /** last MPEG clock reference */
    uint64_t last_pcr;
    /** highest Upipe timestamp given to a frame */
    uint64_t timestamp_highest;

    /** probe to get events from inner pipes */
    struct uprobe plumber;
    /** probe to get events from ts_pmtd inner pipe */
    struct uprobe pmtd_probe;
    /** probe to get events from PCR ts_decaps inner pipe */
    struct uprobe pcr_probe;

    /** list of outputs */
    struct uchain outputs;

    /** manager to create outputs */
    struct upipe_mgr output_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux_program, upipe,
                   UPIPE_TS_DEMUX_PROGRAM_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_demux_program, urefcount,
                       upipe_ts_demux_program_no_input)
UPIPE_HELPER_FLOW(upipe_ts_demux_program, "void.")
UPIPE_HELPER_BIN_OUTPUT(upipe_ts_demux_program, last_inner_probe, last_inner,
                        output, output_request_list)

UPIPE_HELPER_SUBPIPE(upipe_ts_demux, upipe_ts_demux_program, program,
                     program_mgr, programs, uchain)

UBASE_FROM_TO(upipe_ts_demux_program, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ts_demux_program_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of an output of a ts_demux_program
 * inner pipe. */
struct upipe_ts_demux_output {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** PID */
    uint64_t pid;
    /** true if the output is used for PCR */
    bool pcr;
    /** ts_split_output inner pipe */
    struct upipe *split_output;
    /** setrap inner pipe */
    struct upipe *setrap;

    /** maximum retention time in the pipeline */
    uint64_t max_delay;

    /** probe to get events from inner pipes */
    struct uprobe probe;

    /** list of output bin requests */
    struct uchain output_request_list;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;
    /** pointer to the last inner pipe */
    struct upipe *last_inner;
    /** pointer to the output of the last inner pipe */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_demux_output, upipe,
                   UPIPE_TS_DEMUX_OUTPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_demux_output, urefcount,
                       upipe_ts_demux_output_no_input)
UPIPE_HELPER_FLOW(upipe_ts_demux_output, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_ts_demux_output, last_inner_probe, last_inner,
                        output, output_request_list)

UPIPE_HELPER_SUBPIPE(upipe_ts_demux_program, upipe_ts_demux_output, output,
                     output_mgr, outputs, uchain)

UBASE_FROM_TO(upipe_ts_demux_output, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ts_demux_output_free(struct urefcount *urefcount_real);


/*
 * psi_pid structure handling
 */

/** @internal @This is the context of a PID carrying PSI of a ts_demux pipe. */
struct upipe_ts_demux_psi_pid {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** PID */
    uint16_t pid;
    /** pointer to psi_split inner pipe */
    struct upipe *psi_split;
    /** pointer to split_output inner pipe */
    struct upipe *split_output;
    /** reference count */
    unsigned int refcount;
};

UBASE_FROM_TO(upipe_ts_demux_psi_pid, uchain, uchain, uchain)

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
    if (unlikely(psi_pid == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }
    psi_pid->pid = pid;

    /* set PID filter on ts_split inner pipe */
    if (unlikely((psi_pid->psi_split =
                      upipe_void_alloc(ts_demux_mgr->ts_psi_split_mgr,
                                       uprobe_pfx_alloc_va(
                                               uprobe_use(&upipe_ts_demux->proxy_probe),
                                               UPROBE_LOG_VERBOSE,
                                               "psi split %"PRIu16,
                                               pid))) == NULL)) {
        free(psi_pid);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    struct uref *flow_def =
        uref_alloc_control(upipe_ts_demux->flow_def_input->mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegtspsi.")) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, pid)) ||
                 !ubase_check(upipe_set_flow_def(psi_pid->psi_split,
                                  flow_def)))) {
        if (flow_def != NULL)
            uref_free(flow_def);
        free(psi_pid);
        return NULL;
    }

    uref_flow_set_def(flow_def, "block.mpegts.mpegtspsi.");
    psi_pid->split_output =
        upipe_flow_alloc_sub(upipe_ts_demux->split,
                             uprobe_pfx_alloc_va(
                                 uprobe_use(&upipe_ts_demux->psi_pid_plumber),
                                 UPROBE_LOG_VERBOSE,
                                 "split output %"PRIu16, pid),
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
        struct uchain *uchain, *uchain_tmp;
        ulist_delete_foreach (&upipe_ts_demux->psi_pids, uchain, uchain_tmp) {
            if (uchain == upipe_ts_demux_psi_pid_to_uchain(psi_pid)) {
                ulist_delete(uchain);
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

/** @internal @This catches clock_ref events coming from output inner pipes.
 *
 * @param upipe description structure of the pipe
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_output_clock_ref(struct upipe *upipe,
                                           struct upipe *inner,
                                           int event, va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
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
    return UBASE_ERR_NONE;
}

/** @internal @This catches clock_ts events coming from output inner pipes.
 *
 * @param upipe description structure of the pipe
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_output_clock_ts(struct upipe *upipe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);

    struct uref *uref = va_arg(args, struct uref *);
    uint64_t dts_orig;
    if (ubase_check(uref_clock_get_dts_orig(uref, &dts_orig))) {
        /* handle 2^33 wrap-arounds */
        uint64_t delta = (TS_CLOCK_MAX + dts_orig -
                          (program->last_pcr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
        if (delta <= upipe_ts_demux_output->max_delay) {
            uint64_t dts = program->timestamp_offset +
                           program->last_pcr + delta;
            uref_clock_set_dts_prog(uref, dts);
            uint64_t dts_pts_delay = 0;
            uref_clock_get_dts_pts_delay(uref, &dts_pts_delay);
            if (dts + dts_pts_delay > program->timestamp_highest)
                program->timestamp_highest = dts + dts_pts_delay;
            upipe_verbose_va(upipe, "read DTS %"PRIu64" -> %"PRIu64" (pts delay %"PRIu64")",
                             dts_orig, dts, dts_pts_delay);
        } else
            upipe_warn_va(upipe, "too long delay for DTS %"PRIu64" (%"PRIu64")",
                          dts_orig, delta);
    }

    return upipe_throw(upipe, event, uref);
}

/** @internal @This catches need_output events coming from output inner pipes.
 *
 * @param upipe description structure of the pipe
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_output_plumber(struct upipe *upipe,
                                         struct upipe *inner,
                                         int event, va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(
                upipe_ts_demux_program_to_upipe(program)->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return upipe_throw_proxy(upipe, inner, event, args);

    if (!ubase_ncmp(def, "block.mpegts.")) {
        /* allocate ts_decaps inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->ts_decaps_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux_output->probe),
                                    UPROBE_LOG_VERBOSE, "decaps"));
        if (unlikely(output == NULL)) {
            upipe_release(upipe_ts_demux_output->setrap);
            upipe_ts_demux_output->setrap = NULL;
            return UBASE_ERR_ALLOC;
        }
        upipe_release(output);
        return UBASE_ERR_NONE;
    }

    if (!ubase_ncmp(def, "block.mpegtspes.")) {
        /* allocate ts_pesd inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->ts_pesd_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux_output->probe),
                                    UPROBE_LOG_VERBOSE, "pesd"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_release(output);
        return UBASE_ERR_NONE;
    }

    if ((!ubase_ncmp(def, "block.mp2.") ||
         !ubase_ncmp(def, "block.aac.")) &&
        ts_demux_mgr->mpgaf_mgr != NULL) {
        /* allocate mpgaf inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->mpgaf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "mpgaf"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    if ((!ubase_ncmp(def, "block.ac3.") ||
         !ubase_ncmp(def, "block.eac3.")) &&
        ts_demux_mgr->a52f_mgr != NULL) {
        /* allocate a52f inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->a52f_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "a52f"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    if ((!ubase_ncmp(def, "block.mpeg2video.") ||
         !ubase_ncmp(def, "block.mpeg1video.")) &&
        ts_demux_mgr->mpgvf_mgr != NULL) {
        /* allocate mpgvf inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->mpgvf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "mpgvf"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    if (!ubase_ncmp(def, "block.h264.") &&
        ts_demux_mgr->h264f_mgr != NULL) {
        /* allocate h264f inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->h264f_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "h264f"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    if (!ubase_ncmp(def, "block.dvb_teletext.") &&
        ts_demux_mgr->telxf_mgr != NULL) {
        /* allocate telxf inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->telxf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "telxf"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    if (!ubase_ncmp(def, "block.dvb_subtitle.") &&
        ts_demux_mgr->dvbsubf_mgr != NULL) {
        /* allocate dvbsubf inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->dvbsubf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "dvbsubf"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    if (!ubase_ncmp(def, "block.opus.") &&
        ts_demux_mgr->opusf_mgr != NULL) {
        /* allocate opusf inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->opusf_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_output->last_inner_probe),
                    UPROBE_LOG_VERBOSE, "opusf"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_output_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "unknown output flow definition: %s", def);
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This catches events coming from output inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_output
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_output_probe(struct uprobe *uprobe,
                                       struct upipe *inner,
                                       int event, va_list args)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        container_of(uprobe, struct upipe_ts_demux_output, probe);
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);

    switch (event) {
        case UPROBE_CLOCK_REF:
            return upipe_ts_demux_output_clock_ref(upipe, inner, event, args);
        case UPROBE_CLOCK_TS:
            return upipe_ts_demux_output_clock_ts(upipe, inner, event, args);
        case UPROBE_NEED_OUTPUT:
            return upipe_ts_demux_output_plumber(upipe, inner, event, args);
        default:
            return upipe_throw_proxy(upipe, inner, event, args);
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
    upipe_ts_demux_output_init_urefcount(upipe);
    urefcount_init(upipe_ts_demux_output_to_urefcount_real(upipe_ts_demux_output), upipe_ts_demux_output_free);
    upipe_ts_demux_output_init_bin_output(upipe,
            upipe_ts_demux_output_to_urefcount_real(upipe_ts_demux_output));
    upipe_ts_demux_output->pcr = false;
    upipe_ts_demux_output->split_output = NULL;
    upipe_ts_demux_output->setrap = NULL;
    upipe_ts_demux_output->max_delay = MAX_DELAY_STILL;
    uref_ts_flow_get_max_delay(flow_def, &upipe_ts_demux_output->max_delay);
    uprobe_init(&upipe_ts_demux_output->probe,
                upipe_ts_demux_output_probe, NULL);
    upipe_ts_demux_output->probe.refcount =
        upipe_ts_demux_output_to_urefcount_real(upipe_ts_demux_output);

    upipe_ts_demux_output_init_sub(upipe);
    upipe_throw_ready(upipe);

    const char *def;
    if (unlikely(!ubase_check(uref_ts_flow_get_pid(flow_def, &upipe_ts_demux_output->pid)) ||
                 upipe_ts_demux_output->pid >= MAX_PIDS ||
                 !ubase_check(uref_flow_get_raw_def(flow_def, &def)) ||
                 !ubase_check(uref_flow_set_def(flow_def, def)) ||
                 !ubase_check(uref_flow_delete_raw_def(flow_def)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);
    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(
                upipe_ts_demux_program_to_upipe(program)->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);
    /* set up split_output and set rap inner pipes */
    if (unlikely((upipe_ts_demux_output->split_output =
                    upipe_flow_alloc_sub(
                        demux->split,
                        uprobe_pfx_alloc_va(
                            uprobe_use(&upipe_ts_demux_output->probe),
                            UPROBE_LOG_VERBOSE,
                            "split output %"PRIu64, upipe_ts_demux_output->pid),
                        flow_def)) == NULL ||
                 (upipe_ts_demux_output->setrap =
                    upipe_void_alloc_output(upipe_ts_demux_output->split_output,
                               ts_demux_mgr->setrap_mgr,
                               uprobe_pfx_alloc_va(
                                   uprobe_use(&upipe_ts_demux_output->probe),
                                   UPROBE_LOG_VERBOSE, "setrap %"PRIu64,
                                   upipe_ts_demux_output->pid))) == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    uref_free(flow_def);
    upipe_ts_demux_program_check_pcr(upipe_ts_demux_program_to_upipe(program));
    return upipe;
}

/** @internal @This updates an output subpipe with a new flow definition
 * coming from pmtd.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the new flow def couldn't be applied
 */
static bool upipe_ts_demux_output_pmtd_update(struct upipe *upipe,
                                              struct uref *flow_def)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    if (likely(upipe_ts_demux_output->setrap != NULL)) {
        flow_def = uref_dup(flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }

        const char *def;
        if (unlikely(!ubase_check(uref_flow_get_raw_def(flow_def, &def)) ||
                     !ubase_check(uref_flow_set_def(flow_def, def)) ||
                     !ubase_check(uref_flow_delete_raw_def(flow_def)))) {
            uref_free(flow_def);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }

        upipe_set_flow_def(upipe_ts_demux_output->setrap, flow_def);
        int err = UBASE_ERR_NONE;
        if (upipe_ts_demux_output->last_inner != NULL)
            /* also set the framer to catch incompatibilities */
            err = upipe_set_flow_def(upipe_ts_demux_output->last_inner,
                                     flow_def);
        uref_free(flow_def);
        return err == UBASE_ERR_NONE;
    }
    return false;
}

/** @internal @This processes control commands on a ts_demux_output pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_demux_output_control(struct upipe *upipe,
                                         int command, va_list args)
{
    switch (command) {
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_output_get_super(upipe, p);
        }

        default:
            return upipe_ts_demux_output_control_bin_output(upipe, command,
                                                            args);
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_demux_output_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_demux_output_to_upipe(upipe_ts_demux_output);

    upipe_throw_dead(upipe);
    uprobe_clean(&upipe_ts_demux_output->probe);
    urefcount_clean(urefcount_real);
    upipe_ts_demux_output_clean_urefcount(upipe);
    upipe_ts_demux_output_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_output_no_input(struct upipe *upipe)
{
    struct upipe_ts_demux_output *upipe_ts_demux_output =
        upipe_ts_demux_output_from_upipe(upipe);
    struct upipe_ts_demux_program *program =
        upipe_ts_demux_program_from_output_mgr(upipe->mgr);

    if (upipe_ts_demux_output->split_output != NULL)
        upipe_release(upipe_ts_demux_output->split_output);
    if (upipe_ts_demux_output->setrap != NULL)
        upipe_release(upipe_ts_demux_output->setrap);
    upipe_ts_demux_output_clean_bin_output(upipe);
    upipe_ts_demux_output_clean_sub(upipe);
    upipe_ts_demux_program_check_pcr(upipe_ts_demux_program_to_upipe(program));
    urefcount_release(upipe_ts_demux_output_to_urefcount_real(upipe_ts_demux_output));
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
    output_mgr->refcount = upipe_ts_demux_program_to_urefcount_real(program);
    output_mgr->signature = UPIPE_TS_DEMUX_OUTPUT_SIGNATURE;
    output_mgr->upipe_alloc = upipe_ts_demux_output_alloc;
    output_mgr->upipe_input = NULL;
    output_mgr->upipe_control = upipe_ts_demux_output_control;
    output_mgr->upipe_mgr_control = NULL;
}


/*
 * upipe_ts_demux_program structure handling (derived from upipe structure)
 */

/** @internal @This catches need_output events coming from program inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_plumber(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
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
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return upipe_throw_proxy(upipe, inner, event, args);

    if (!ubase_ncmp(def, "block.mpegtspsi.mpegtspmt.")) {
        /* allocate ts_pmtd inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->ts_pmtd_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_demux_program->pmtd_probe),
                             UPROBE_LOG_VERBOSE, "pmtd"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_ts_demux_program_store_last_inner(upipe, output);
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "unknown program flow definition: %s", def);
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This catches new_flow_def events coming from pmtd inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param pmtd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_pmtd_new_flow_def(
        struct upipe *upipe, struct upipe *pmtd, int event, va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);

    struct uref *uref = va_arg(args, struct uref *);
    uint64_t pmtd_pcrpid;

    UBASE_RETURN(uref_ts_flow_get_pcr_pid(uref, &pmtd_pcrpid))
    if (upipe_ts_demux_program->pcr_pid != pmtd_pcrpid) {
        if (upipe_ts_demux_program->pcr_split_output != NULL) {
            upipe_release(upipe_ts_demux_program->pcr_split_output);
            upipe_ts_demux_program->pcr_split_output = NULL;
        }

        upipe_ts_demux_program->pcr_pid = pmtd_pcrpid;
        upipe_ts_demux_program_check_pcr(upipe);
    }

    upipe_throw_new_flow_def(upipe, uref);
    return UBASE_ERR_NONE;
}

/** @internal @This catches split_update events coming from pmtd inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param pmtd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_pmtd_update(struct upipe *upipe,
        struct upipe *pmtd, int event, va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);

    /* send the event upstream */
    upipe_split_throw_update(upipe);

    /* send source_end on the removed or changed outputs */
    struct uchain *uchain;
    struct upipe_ts_demux_output *output = NULL;
    ulist_foreach (&upipe_ts_demux_program->outputs, uchain) {
        if (output != NULL)
            upipe_release(upipe_ts_demux_output_to_upipe(output));
        output = upipe_ts_demux_output_from_uchain(uchain);
        /* to avoid having the uchain disappear during upipe_throw_source_end */
        upipe_use(upipe_ts_demux_output_to_upipe(output));

        struct uref *flow_def = NULL;
        uint64_t id = 0;
        while (ubase_check(upipe_split_iterate(pmtd, &flow_def)) &&
               flow_def != NULL)
            if (ubase_check(uref_flow_get_id(flow_def, &id)) &&
                id == output->pid) {
                if (!upipe_ts_demux_output_pmtd_update(
                        upipe_ts_demux_output_to_upipe(output), flow_def))
                    upipe_throw_source_end(
                            upipe_ts_demux_output_to_upipe(output));
                break;
            }
        if (id != output->pid)
            upipe_throw_source_end(upipe_ts_demux_output_to_upipe(output));
    }
    if (output != NULL)
        upipe_release(upipe_ts_demux_output_to_upipe(output));
    return UBASE_ERR_NONE;
}

/** @internal @This catches events coming from pmtd inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param pmtd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_pmtd_probe(struct uprobe *uprobe,
                                             struct upipe *pmtd,
                                             int event, va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pmtd_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    switch (event) {
        case UPROBE_NEW_RAP: {
            struct uref *uref = va_arg(args, struct uref *);
            uint64_t pmt_rap;
            if (ubase_check(uref_clock_get_rap_sys(uref, &pmt_rap)))
                upipe_ts_demux_program->pmt_rap = pmt_rap;
            return UBASE_ERR_NONE;
        }
        case UPROBE_NEW_FLOW_DEF:
            return upipe_ts_demux_program_pmtd_new_flow_def(upipe, pmtd, event,
                                                            args);
        case UPROBE_SPLIT_UPDATE:
            return upipe_ts_demux_program_pmtd_update(upipe, pmtd, event, args);
        default:
            return upipe_throw_proxy(upipe, pmtd, event, args);
    }
}

/** @internal @This catches events coming from PCR ts_decaps inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_pcr_probe(struct uprobe *uprobe,
                                            struct upipe *inner,
                                            int event, va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, pcr_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    if (event != UPROBE_CLOCK_REF)
        return UBASE_ERR_UNHANDLED;

    struct uref *uref = va_arg(args, struct uref *);
    uint64_t pcr_orig = va_arg(args, uint64_t);
    int discontinuity = va_arg(args, int);
    upipe_ts_demux_program_handle_pcr(upipe, uref, pcr_orig, discontinuity);
    return UBASE_ERR_NONE;
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
    upipe_verbose_va(upipe, "read PCR %"PRIu64, pcr_orig);

    /* handle 2^33 wrap-arounds */
    uint64_t delta =
        (TS_CLOCK_MAX + pcr_orig -
         (upipe_ts_demux_program->last_pcr % TS_CLOCK_MAX)) % TS_CLOCK_MAX;
    if (delta <= MAX_PCR_INTERVAL && !discontinuity)
        upipe_ts_demux_program->last_pcr += delta;
    else {
        /* FIXME same clock for all programs */
        upipe_warn_va(upipe, "PCR discontinuity %"PRIu64, delta);
        upipe_ts_demux_program->last_pcr = pcr_orig;
        upipe_ts_demux_program->timestamp_offset =
            upipe_ts_demux_program->timestamp_highest - pcr_orig;
        discontinuity = 1;
    }
    upipe_throw_clock_ref(upipe, uref,
                          upipe_ts_demux_program->last_pcr +
                          upipe_ts_demux_program->timestamp_offset,
                          discontinuity);

    if (upipe_ts_demux_program->pmt_rap) {
        struct uchain *uchain;
        struct upipe_ts_demux_output *output = NULL;
        uint64_t pcr_rap = upipe_ts_demux_program->pmt_rap;
        ulist_foreach (&upipe_ts_demux_program->outputs, uchain) {
            output = upipe_ts_demux_output_from_uchain(uchain);
            if (output->setrap != NULL) {
                UBASE_FATAL(upipe, upipe_setrap_set_rap(output->setrap,
                                                        pcr_rap));
            }
        }
        /* this is also valid for the packet we are processing */
        uref_clock_set_rap_sys(uref, pcr_rap);
    }
}

/** @internal @This checks whether there is a ts_decaps on the PID
 * carrying the PCR, and otherwise allocates/deallocates one.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_check_pcr(struct upipe *upipe)
{
    if (upipe_dead(upipe))
        return;

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

    struct uref *flow_def =
        uref_alloc_control(upipe_ts_demux_program->flow_def_input->mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegts.")) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def,
                                       upipe_ts_demux_program->pcr_pid)))) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_demux_program->pcr_split_output =
        upipe_flow_alloc_sub(demux->split,
                     uprobe_pfx_alloc_va(
                         uprobe_use(upipe_ts_demux_to_upipe(demux)->uprobe),
                         UPROBE_LOG_VERBOSE, "split output PCR %"PRIu64,
                         upipe_ts_demux_program->pcr_pid),
                     flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_program->pcr_split_output == NULL ||
                 !ubase_check(upipe_get_flow_def(
                         upipe_ts_demux_program->pcr_split_output,
                         &flow_def)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    struct upipe *decaps =
        upipe_void_alloc_output(upipe_ts_demux_program->pcr_split_output,
                           ts_demux_mgr->ts_decaps_mgr,
                           uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_demux_program->pcr_probe),
                             UPROBE_LOG_VERBOSE,
                             "decaps PCR %"PRIu64,
                             upipe_ts_demux_program->pcr_pid));
    if (unlikely(decaps == NULL ||
                 !ubase_check(upipe_set_output(decaps, demux->null)))) {
        if (decaps != NULL)
            upipe_release(decaps);
        upipe_release(upipe_ts_demux_program->pcr_split_output);
        upipe_ts_demux_program->pcr_split_output = NULL;
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
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
    upipe_ts_demux_program_init_urefcount(upipe);
    urefcount_init(upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program), upipe_ts_demux_program_free);
    upipe_ts_demux_program_init_bin_output(upipe,
            upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program));
    upipe_ts_demux_program_init_output_mgr(upipe);
    upipe_ts_demux_program_init_sub_outputs(upipe);
    upipe_ts_demux_program->flow_def_input = flow_def;
    upipe_ts_demux_program->program = 0;
    upipe_ts_demux_program->pmt_rap = 0;
    upipe_ts_demux_program->pcr_pid = 0;
    upipe_ts_demux_program->pcr_split_output = NULL;
    upipe_ts_demux_program->psi_split_output = NULL;
    upipe_ts_demux_program->timestamp_offset = 0;
    upipe_ts_demux_program->timestamp_highest = TS_CLOCK_MAX;
    upipe_ts_demux_program->last_pcr = TS_CLOCK_MAX;
    uprobe_init(&upipe_ts_demux_program->plumber,
                upipe_ts_demux_program_plumber, NULL);
    upipe_ts_demux_program->plumber.refcount =
        upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program);
    uprobe_init(&upipe_ts_demux_program->pmtd_probe,
                upipe_ts_demux_program_pmtd_probe,
                &upipe_ts_demux_program->last_inner_probe);
    upipe_ts_demux_program->pmtd_probe.refcount =
        upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program);
    uprobe_init(&upipe_ts_demux_program->pcr_probe,
                upipe_ts_demux_program_pcr_probe, NULL);
    upipe_ts_demux_program->pcr_probe.refcount =
        upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program);

    upipe_ts_demux_program_init_sub(upipe);
    upipe_throw_ready(upipe);

    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(upipe->mgr);
    const uint8_t *filter, *mask;
    size_t size;
    const char *def;
    flow_def = uref_dup(flow_def);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_ts_flow_get_pid(flow_def,
                     &upipe_ts_demux_program->pmt_pid)) ||
                 upipe_ts_demux_program->pmt_pid >= MAX_PIDS ||
                 !ubase_check(uref_ts_flow_get_psi_filter(flow_def, &filter, &mask,
                                              &size)) ||
                 !ubase_check(uref_flow_get_id(flow_def,
                                   &upipe_ts_demux_program->program)) ||
                 upipe_ts_demux_program->program == 0 ||
                 upipe_ts_demux_program->program > UINT16_MAX ||
                 !ubase_check(uref_flow_get_raw_def(flow_def, &def)) ||
                 !ubase_check(uref_flow_set_def(flow_def, def)) ||
                 !ubase_check(uref_flow_delete_raw_def(flow_def)) ||
                 (upipe_ts_demux_program->psi_pid =
                      upipe_ts_demux_psi_pid_use(upipe_ts_demux_to_upipe(demux),
                          upipe_ts_demux_program->pmt_pid)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    upipe_ts_demux_program->psi_split_output =
        upipe_flow_alloc_sub(upipe_ts_demux_program->psi_pid->psi_split,
                             uprobe_pfx_alloc(
                                 uprobe_use(&upipe_ts_demux_program->plumber),
                                 UPROBE_LOG_VERBOSE, "psi_split output"),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_program->psi_split_output == NULL)) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
        upipe_ts_demux_program->psi_pid = NULL;
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    return upipe;
}

/** @internal @This processes control commands on a ts_demux_program pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_demux_program_control(struct upipe *upipe,
                                          int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_demux_program_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_program_iterate_sub(upipe, p);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_program_get_super(upipe, p);
        }

        default:
            return upipe_ts_demux_program_control_bin_output(upipe, command,
                                                             args);
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_demux_program_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_urefcount_real(urefcount_real);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    upipe_throw_dead(upipe);

    uprobe_clean(&upipe_ts_demux_program->plumber);
    uprobe_clean(&upipe_ts_demux_program->pmtd_probe);
    uprobe_clean(&upipe_ts_demux_program->pcr_probe);
    urefcount_clean(urefcount_real);
    upipe_ts_demux_program_clean_sub_outputs(upipe);
    if (upipe_ts_demux_program->flow_def_input != NULL)
        uref_free(upipe_ts_demux_program->flow_def_input);
    upipe_ts_demux_program_clean_urefcount(upipe);
    upipe_ts_demux_program_free_flow(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_no_input(struct upipe *upipe)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct upipe_ts_demux *demux =
        upipe_ts_demux_from_program_mgr(upipe->mgr);
    upipe_ts_demux_program_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    /* close PMT to release ESs */
    if (upipe_ts_demux_program->psi_split_output != NULL) {
        upipe_release(upipe_ts_demux_program->psi_split_output);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
        upipe_ts_demux_program->psi_split_output = NULL;
    }
    upipe_ts_demux_program_store_last_inner(upipe, NULL);
    upipe_split_throw_update(upipe);

    if (upipe_ts_demux_program->psi_split_output != NULL) {
        upipe_release(upipe_ts_demux_program->psi_split_output);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_to_upipe(demux),
                                       upipe_ts_demux_program->psi_pid);
    }
    if (upipe_ts_demux_program->pcr_split_output != NULL)
        upipe_release(upipe_ts_demux_program->pcr_split_output);
    upipe_ts_demux_program_clean_bin_output(upipe);
    upipe_ts_demux_program_clean_sub(upipe);
    urefcount_release(upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program));
}

/** @internal @This initializes the program manager for a ts_demux pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_demux->program_mgr;
    program_mgr->refcount = upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    program_mgr->signature = UPIPE_TS_DEMUX_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_demux_program_alloc;
    program_mgr->upipe_input = NULL;
    program_mgr->upipe_control = upipe_ts_demux_program_control;
    program_mgr->upipe_mgr_control = NULL;
}


/*
 * upipe_ts_demux structure handling (derived from upipe structure)
 */

/** @internal @This catches need_output events coming from inner pipes created
 * by psi_pid objects.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_psi_pid_plumber(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, psi_pid_plumber);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return upipe_throw_proxy(upipe, inner, event, args);

    if (!ubase_ncmp(def, "block.mpegts.")) {
        /* allocate ts_decaps inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->ts_decaps_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->psi_pid_plumber),
                                 UPROBE_LOG_VERBOSE, "decaps"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_release(output);
        return UBASE_ERR_NONE;
    }

    if (!ubase_ncmp(def, "block.mpegtspsi.")) {
        /* allocate ts_psim inner */
        struct upipe *output =
            upipe_void_alloc_output(inner, ts_demux_mgr->ts_psim_mgr,
                uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->psim_probe),
                                 UPROBE_LOG_VERBOSE, "psim"));
        if (unlikely(output == NULL))
            return UBASE_ERR_ALLOC;
        upipe_release(output);
        return UBASE_ERR_NONE;
    }

    upipe_warn_va(upipe, "unknown psi_pid flow definition: %s", def);
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This catches the new_flow_def events coming from psim inner
 * pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param psim pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_psim_probe(struct uprobe *uprobe, struct upipe *psim,
                                     int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, psim_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
            return uprobe_throw_va(upipe->uprobe, psim, event, args);
        default:
            break;
    }

    struct uref *flow_def;
    const char *def;
    if (!uprobe_plumber(event, args, &flow_def, &def))
        return upipe_throw_proxy(upipe, psim, event, args);

    uint64_t pid;
    UBASE_RETURN(uref_ts_flow_get_pid(flow_def, &pid))

    struct upipe_ts_demux_psi_pid *psi_pid =
        upipe_ts_demux_psi_pid_find(upipe, pid);
    if (unlikely(psi_pid == NULL)) {
        upipe_warn_va(upipe, "unknown PSI PID %"PRIu64, pid);
        return UBASE_ERR_INVALID;
    }

    return upipe_set_output(psim, psi_pid->psi_split);
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
            upipe_ts_demux->conformance = UPIPE_TS_CONFORMANCE_ISO;
            break;
        case 16:
            /* Mandatory PID in DVB systems */
            upipe_ts_demux->conformance = UPIPE_TS_CONFORMANCE_DVB;
            break;
    }
}

/** @internal @This catches new_rap events coming from patd inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param patd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_patd_new_rap(struct upipe *upipe,
                                       struct upipe *patd,
                                       int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);

    struct uref *uref = va_arg(args, struct uref *);
    assert(uref != NULL);

    uint64_t pat_rap;
    UBASE_RETURN(uref_clock_get_rap_sys(uref, &pat_rap))
    return upipe_setrap_set_rap(upipe_ts_demux->setrap, pat_rap);
}

/** @internal @This catches update events coming from patd inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param patd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_patd_update(struct upipe *upipe,
                                      struct upipe *patd,
                                      int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);

    struct uref *nit;
    if (ubase_check(upipe_ts_patd_get_nit(patd, &nit))) {
        upipe_ts_demux->nit_pid = 0;
        uref_ts_flow_get_pid(nit, &upipe_ts_demux->nit_pid);
        upipe_ts_demux_conformance_guess(upipe);
    }

    /* send source_end on the program */
    struct uchain *uchain;
    struct upipe_ts_demux_program *program = NULL;
    ulist_foreach (&upipe_ts_demux->programs, uchain) {
        if (program != NULL)
            upipe_release(upipe_ts_demux_program_to_upipe(program));
        program = upipe_ts_demux_program_from_uchain(uchain);
        /* to avoid having the uchain disappear during upipe_throw_source_end */
        upipe_use(upipe_ts_demux_program_to_upipe(program));

        struct uref *flow_def = NULL;
        uint64_t id = 0, pid = 0;
        while (ubase_check(upipe_split_iterate(patd, &flow_def)) &&
               flow_def == NULL)
            if (ubase_check(uref_flow_get_id(flow_def, &id)) &&
                id == program->program &&
                ubase_check(uref_ts_flow_get_pid(flow_def, &pid)) &&
                pid == program->pmt_pid)
                break;
        if (id != program->program || pid != program->pmt_pid)
            upipe_throw_source_end(upipe_ts_demux_program_to_upipe(program));
    }
    if (program != NULL)
        upipe_release(upipe_ts_demux_program_to_upipe(program));

    /* send the event upstream */
    return upipe_split_throw_update(upipe);
}

/** @internal @This catches events coming from patd inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param patd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_patd_probe(struct uprobe *uprobe,
                                     struct upipe *patd,
                                     int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, patd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_NEW_RAP:
            return upipe_ts_demux_patd_new_rap(upipe, patd, event, args);
        case UPROBE_SPLIT_UPDATE:
            return upipe_ts_demux_patd_update(upipe, patd, event, args);
        default:
            return upipe_throw_proxy(upipe, patd, event, args);
    }
}

/** @internal @This catches events coming from sync or check inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_input_probe(struct uprobe *uprobe,
                                      struct upipe *inner,
                                      int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, input_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_SYNC_ACQUIRED:
            return upipe_ts_demux_sync_acquired(upipe);
        case UPROBE_SYNC_LOST:
            return upipe_ts_demux_sync_lost(upipe);
        default:
            return upipe_throw_proxy(upipe, inner, event, args);
    }
}

/** @internal @This catches events coming from split inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param subpipe pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_split_probe(struct uprobe *uprobe,
                                      struct upipe *inner,
                                      int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, split_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_TS_SPLIT_ADD_PID:
        case UPROBE_TS_SPLIT_DEL_PID:
        default:
            return upipe_throw_proxy(upipe, inner, event, args);
    }
}

/** @internal @This catches events coming from other inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_proxy_probe(struct uprobe *uprobe,
                                      struct upipe *inner,
                                      int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, proxy_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);
    return upipe_throw_proxy(upipe, inner, event, args);
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
    struct upipe *upipe = upipe_ts_demux_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    upipe_ts_demux_init_urefcount(upipe);
    urefcount_init(upipe_ts_demux_to_urefcount_real(upipe_ts_demux),
                   upipe_ts_demux_free);
    upipe_ts_demux_init_bin_input(upipe);
    upipe_ts_demux_init_bin_output(upipe,
            upipe_ts_demux_to_urefcount_real(upipe_ts_demux));
    upipe_ts_demux_init_program_mgr(upipe);
    upipe_ts_demux_init_sub_programs(upipe);

    upipe_ts_demux_init_sync(upipe);
    upipe_ts_demux->input = upipe_ts_demux->split = upipe_ts_demux->setrap =
        upipe_ts_demux->null = upipe_ts_demux->psi_split_output_pat = NULL;
    upipe_ts_demux->psi_pid_pat = NULL;

    ulist_init(&upipe_ts_demux->psi_pids);
    upipe_ts_demux->conformance = UPIPE_TS_CONFORMANCE_ISO;
    upipe_ts_demux->auto_conformance = true;
    upipe_ts_demux->nit_pid = 0;
    upipe_ts_demux->flow_def_input = NULL;

    uprobe_init(&upipe_ts_demux->psi_pid_plumber,
                upipe_ts_demux_psi_pid_plumber, NULL);
    upipe_ts_demux->psi_pid_plumber.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->psim_probe, upipe_ts_demux_psim_probe, NULL);
    upipe_ts_demux->psim_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->patd_probe,
                upipe_ts_demux_patd_probe, &upipe_ts_demux->last_inner_probe);
    upipe_ts_demux->patd_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->input_probe,
                upipe_ts_demux_input_probe, NULL);
    upipe_ts_demux->input_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->split_probe, upipe_ts_demux_split_probe, NULL);
    upipe_ts_demux->split_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->proxy_probe, upipe_ts_demux_proxy_probe, NULL);
    upipe_ts_demux->proxy_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);

    upipe_throw_ready(upipe);
    upipe_ts_demux_sync_lost(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_demux_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->input != NULL) {
        UBASE_RETURN(upipe_set_flow_def(upipe_ts_demux->input, flow_def));
    }

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    if (upipe_ts_demux->flow_def_input != NULL)
        uref_free(upipe_ts_demux->flow_def_input);
    upipe_ts_demux->flow_def_input = flow_def_dup;
    if (upipe_ts_demux->input != NULL)
        return UBASE_ERR_NONE;

    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    bool ret;
    struct upipe *input;
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF_SYNC)) {
        if (!ubase_ncmp(def, EXPECTED_FLOW_DEF_CHECK))
            /* allocate ts_check inner pipe */
            input = upipe_void_alloc(ts_demux_mgr->ts_check_mgr,
                     uprobe_pfx_alloc(
                         uprobe_use(&upipe_ts_demux->proxy_probe),
                         UPROBE_LOG_VERBOSE, "check"));
        else
            /* allocate ts_sync inner pipe */
            input = upipe_void_alloc(ts_demux_mgr->ts_sync_mgr,
                     uprobe_pfx_alloc(
                         uprobe_use(&upipe_ts_demux->proxy_probe),
                         UPROBE_LOG_VERBOSE, "sync"));
        if (unlikely(input == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        ret = ubase_check(upipe_set_flow_def(input, flow_def));
        assert(ret);
        upipe_ts_demux_store_first_inner(upipe, input);

        upipe_ts_demux->setrap =
            upipe_void_alloc_output(upipe_ts_demux->input,
                         ts_demux_mgr->setrap_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_demux->proxy_probe),
                             UPROBE_LOG_VERBOSE, "setrap"));
        if (unlikely(upipe_ts_demux->setrap == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    } else {
        upipe_ts_demux->setrap =
            upipe_void_alloc(ts_demux_mgr->setrap_mgr,
                             uprobe_pfx_alloc(
                                 uprobe_use(&upipe_ts_demux->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "setrap"));
        if (unlikely(upipe_ts_demux->setrap == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        ret = ubase_check(upipe_set_flow_def(upipe_ts_demux->setrap, flow_def));
        assert(ret);

        upipe_ts_demux_store_first_inner(upipe,
                                         upipe_use(upipe_ts_demux->setrap));
        upipe_ts_demux_sync_acquired(upipe);
    }

    upipe_ts_demux->split =
        upipe_void_alloc_output(upipe_ts_demux->setrap,
                   ts_demux_mgr->ts_split_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->split_probe),
                                    UPROBE_LOG_VERBOSE, "split"));
    if (unlikely(upipe_ts_demux->split == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_ts_demux->null =
        upipe_void_alloc(ts_demux_mgr->null_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_demux->proxy_probe),
                             UPROBE_LOG_NOTICE, "null"));
    if (unlikely(upipe_ts_demux->null == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    /* get psi_split inner pipe */
    upipe_ts_demux->psi_pid_pat = upipe_ts_demux_psi_pid_use(upipe, 0);
    if (unlikely(upipe_ts_demux->psi_pid_pat == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
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
    flow_def = uref_alloc_control(upipe_ts_demux->flow_def_input->mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegtspsi.mpegtspat.")) ||
                 !ubase_check(uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE_SYNTAX1)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, 0)) ||
                 (upipe_ts_demux->psi_split_output_pat =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux->psi_pid_pat->psi_split,
                          uprobe_pfx_alloc(
                              uprobe_use(&upipe_ts_demux->proxy_probe),
                              UPROBE_LOG_VERBOSE, "psi_split output"),
                          flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    uref_free(flow_def);
    upipe_get_flow_def(upipe_ts_demux->psi_split_output_pat, &flow_def);

    /* allocate PAT decoder */
    struct upipe *patd =
        upipe_void_alloc_output(upipe_ts_demux->psi_split_output_pat,
                   ts_demux_mgr->ts_patd_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->patd_probe),
                                    UPROBE_LOG_VERBOSE, "patd"));
    if (unlikely(patd == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_demux_store_last_inner(upipe, patd);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the currently detected conformance mode. It cannot
 * return UPIPE_TS_CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return an error code
 */
static int
    _upipe_ts_demux_get_conformance(struct upipe *upipe,
                                    enum upipe_ts_conformance *conformance_p)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    assert(conformance_p != NULL);
    *conformance_p = upipe_ts_demux->conformance;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the conformance mode.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 * @return an error code
 */
static int _upipe_ts_demux_set_conformance(struct upipe *upipe,
                                enum upipe_ts_conformance conformance)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    switch (conformance) {
        case UPIPE_TS_CONFORMANCE_AUTO:
            upipe_ts_demux->auto_conformance = true;
            upipe_ts_demux_conformance_guess(upipe);
            break;
        case UPIPE_TS_CONFORMANCE_ISO:
        case UPIPE_TS_CONFORMANCE_DVB:
        case UPIPE_TS_CONFORMANCE_ATSC:
        case UPIPE_TS_CONFORMANCE_ISDB:
            upipe_ts_demux->auto_conformance = false;
            upipe_ts_demux->conformance = conformance;
            break;
        default:
            return UBASE_ERR_INVALID;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts_demux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_demux_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_demux_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_demux_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_iterate_sub(upipe, p);
        }

        case UPIPE_TS_DEMUX_GET_CONFORMANCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_DEMUX_SIGNATURE)
            enum upipe_ts_conformance *conformance_p =
                va_arg(args, enum upipe_ts_conformance *);
            return _upipe_ts_demux_get_conformance(upipe, conformance_p);
        }
        case UPIPE_TS_DEMUX_SET_CONFORMANCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_DEMUX_SIGNATURE)
            enum upipe_ts_conformance conformance =
                va_arg(args, enum upipe_ts_conformance);
            return _upipe_ts_demux_set_conformance(upipe, conformance);
        }

        default:
            break;
    }

    int err = upipe_ts_demux_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_ts_demux_control_bin_output(upipe, command, args);
    return err;
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_demux_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_demux *upipe_ts_demux =
        upipe_ts_demux_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);
    upipe_throw_dead(upipe);
    uprobe_clean(&upipe_ts_demux->psi_pid_plumber);
    uprobe_clean(&upipe_ts_demux->psim_probe);
    uprobe_clean(&upipe_ts_demux->patd_probe);
    uprobe_clean(&upipe_ts_demux->input_probe);
    uprobe_clean(&upipe_ts_demux->split_probe);
    uref_free(upipe_ts_demux->flow_def_input);
    upipe_ts_demux_clean_sub_programs(upipe);
    upipe_ts_demux_clean_sync(upipe);
    urefcount_clean(urefcount_real);
    upipe_ts_demux_clean_urefcount(upipe);
    upipe_ts_demux_free_void(upipe);
}

/** @This is called when there is no external to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_no_input(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    upipe_ts_demux_throw_sub_programs(upipe, UPROBE_SOURCE_END);
    /* close PAT to release programs */
    if (upipe_ts_demux->psi_split_output_pat != NULL) {
        upipe_release(upipe_ts_demux->psi_split_output_pat);
        upipe_ts_demux->psi_split_output_pat = NULL;
    }
    if (upipe_ts_demux->psi_pid_pat != NULL) {
        upipe_ts_demux_psi_pid_release(upipe, upipe_ts_demux->psi_pid_pat);
        upipe_ts_demux->psi_pid_pat = NULL;
    }
    upipe_ts_demux_store_last_inner(upipe, NULL);
    upipe_split_throw_update(upipe);

    upipe_ts_demux_store_first_inner(upipe, NULL);
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
    upipe_ts_demux_clean_bin_input(upipe);
    upipe_ts_demux_clean_bin_output(upipe);
    urefcount_release(upipe_ts_demux_to_urefcount_real(upipe_ts_demux));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_ts_demux_mgr_free(struct urefcount *urefcount)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_urefcount(urefcount);
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
    if (ts_demux_mgr->mpgaf_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->mpgaf_mgr);
    if (ts_demux_mgr->a52f_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->a52f_mgr);
    if (ts_demux_mgr->mpgvf_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->mpgvf_mgr);
    if (ts_demux_mgr->h264f_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->h264f_mgr);
    if (ts_demux_mgr->telxf_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->telxf_mgr);
    if (ts_demux_mgr->dvbsubf_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->dvbsubf_mgr);
    if (ts_demux_mgr->opusf_mgr != NULL)
        upipe_mgr_release(ts_demux_mgr->opusf_mgr);

    urefcount_clean(urefcount);
    free(ts_demux_mgr);
}

/** @This processes control commands on a ts_demux manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_demux_mgr_control(struct upipe_mgr *mgr,
                                      int command, va_list args)
{
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_TS_DEMUX_MGR_GET_##NAME##_MGR: {                         \
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_DEMUX_SIGNATURE)           \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = ts_demux_mgr->name##_mgr;                                  \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_TS_DEMUX_MGR_SET_##NAME##_MGR: {                         \
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_DEMUX_SIGNATURE)           \
            if (!urefcount_single(&ts_demux_mgr->urefcount))                \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(ts_demux_mgr->name##_mgr);                    \
            ts_demux_mgr->name##_mgr = upipe_mgr_use(m);                    \
            return UBASE_ERR_NONE;                                          \
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

        GET_SET_MGR(mpgaf, MPGAF)
        GET_SET_MGR(a52f, A52F)
        GET_SET_MGR(mpgvf, MPGVF)
        GET_SET_MGR(h264f, H264F)
        GET_SET_MGR(telxf, TELXF)
        GET_SET_MGR(dvbsubf, DVBSUBF)
        GET_SET_MGR(opusf, OPUSF)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
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

    ts_demux_mgr->mpgaf_mgr = NULL;
    ts_demux_mgr->a52f_mgr = NULL;
    ts_demux_mgr->mpgvf_mgr = NULL;
    ts_demux_mgr->h264f_mgr = NULL;
    ts_demux_mgr->telxf_mgr = NULL;
    ts_demux_mgr->dvbsubf_mgr = NULL;
    ts_demux_mgr->opusf_mgr = NULL;

    urefcount_init(upipe_ts_demux_mgr_to_urefcount(ts_demux_mgr),
                   upipe_ts_demux_mgr_free);
    ts_demux_mgr->mgr.refcount = upipe_ts_demux_mgr_to_urefcount(ts_demux_mgr);
    ts_demux_mgr->mgr.signature = UPIPE_TS_DEMUX_SIGNATURE;
    ts_demux_mgr->mgr.upipe_alloc = upipe_ts_demux_alloc;
    ts_demux_mgr->mgr.upipe_input = upipe_ts_demux_bin_input;
    ts_demux_mgr->mgr.upipe_control = upipe_ts_demux_control;
    ts_demux_mgr->mgr.upipe_mgr_control = upipe_ts_demux_mgr_control;
    return upipe_ts_demux_mgr_to_upipe_mgr(ts_demux_mgr);
}

/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
 *
 * Normative references:
 *  - ISO/IEC 13818-1:2007(E) (MPEG-2 Systems)
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
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
#include <upipe/upipe_helper_uref_mgr.h>
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
#include <upipe-ts/upipe_ts_eit_decoder.h>
#include <upipe-ts/upipe_ts_nit_decoder.h>
#include <upipe-ts/upipe_ts_psi_merge.h>
#include <upipe-ts/upipe_ts_psi_split.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>
#include <upipe-ts/upipe_ts_pes_decaps.h>
#include <upipe-ts/upipe_ts_sdt_decoder.h>
#include <upipe-ts/upipe_ts_tdt_decoder.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

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
    /** pointer to ts_nitd manager */
    struct upipe_mgr *ts_nitd_mgr;
    /** pointer to ts_sdtd manager */
    struct upipe_mgr *ts_sdtd_mgr;
    /** pointer to ts_tdtd manager */
    struct upipe_mgr *ts_tdtd_mgr;
    /** pointer to ts_pmtd manager */
    struct upipe_mgr *ts_pmtd_mgr;
    /** pointer to ts_eitd manager */
    struct upipe_mgr *ts_eitd_mgr;

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

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain output_request_list;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** pointer to input inner pipe */
    struct upipe *input;
    /** true if we have thrown the sync_acquired event */
    bool acquired;
    /** flow definition of the input */
    struct uref *flow_def_input;

    /** pointer to null inner pipe */
    struct upipe *null;
    /** pointer to setrap inner pipe */
    struct upipe *setrap;
    /** pointer to ts_split inner pipe */
    struct upipe *split;

    /** psi_pid structure for PAT */
    struct upipe_ts_demux_psi_pid *psi_pid_pat;
    /** ts_psi_split_output inner pipe for PAT */
    struct upipe *psi_split_output_pat;
    /** pointer to ts_patd inner pipe */
    struct upipe *patd;
    /** list of available programs (from PAT) */
    struct uchain pat_programs;

    /** psi_pid structure for NIT */
    struct upipe_ts_demux_psi_pid *psi_pid_nit;
    /** ts_psi_split_output inner pipe for NIT */
    struct upipe *psi_split_output_nit;
    /** pointer to optional ts_nitd inner pipe */
    struct upipe *nitd;

    /** psi_pid structure for SDT */
    struct upipe_ts_demux_psi_pid *psi_pid_sdt;
    /** ts_psi_split_output inner pipe for SDT */
    struct upipe *psi_split_output_sdt;
    /** pointer to optional ts_sdtd inner pipe */
    struct upipe *sdtd;

    /** psi_pid structure for TDT */
    struct upipe_ts_demux_psi_pid *psi_pid_tdt;
    /** ts_psi_split_output inner pipe for TDT */
    struct upipe *psi_split_output_tdt;
    /** pointer to optional ts_tdtd inner pipe */
    struct upipe *tdtd;

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
    /** probe to get events from ts_nitd inner pipe */
    struct uprobe nitd_probe;
    /** probe to get events from ts_sdtd inner pipe */
    struct uprobe sdtd_probe;
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
UPIPE_HELPER_OUTPUT(upipe_ts_demux, output, flow_def, output_state,
                    output_request_list)
UPIPE_HELPER_SYNC(upipe_ts_demux, acquired)
UPIPE_HELPER_BIN_INPUT(upipe_ts_demux, input, input_request_list)
UPIPE_HELPER_UREF_MGR(upipe_ts_demux, uref_mgr, uref_mgr_request, NULL,
                      upipe_ts_demux_register_output_request,
                      upipe_ts_demux_unregister_output_request)

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

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain output_request_list;

    /** flow definition of the input */
    struct uref *flow_def_input;
    /** program number */
    uint64_t program;
    /** psi_pid structure for PMT */
    struct upipe_ts_demux_psi_pid *psi_pid_pmt;
    /** ts_psi_split_output inner pipe */
    struct upipe *psi_split_output_pmt;
    /** pointer to ts_pmtd inner pipe */
    struct upipe *pmtd;
    /** systime_rap of the last PMT */
    uint64_t pmt_rap;

    /** psi_pid structure for EIT */
    struct upipe_ts_demux_psi_pid *psi_pid_eit;
    /** ts_psi_split_output inner pipe for EIT */
    struct upipe *psi_split_output_eit;
    /** pointer to optional ts_eitd inner pipe */
    struct upipe *eitd;

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

    /** probe to get events from ts_pmtd inner pipe */
    struct uprobe pmtd_probe;
    /** probe to get events from ts_eitd inner pipe */
    struct uprobe eitd_probe;
    /** probe to get events from PCR ts_decaps inner pipe */
    struct uprobe pcr_probe;
    /** probe to proxify events from other pipes */
    struct uprobe proxy_probe;

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
UPIPE_HELPER_OUTPUT(upipe_ts_demux_program, output, flow_def, output_state,
                    output_request_list)

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

    upipe_ts_demux_demand_uref_mgr(upipe);
    struct uref *flow_def = uref_alloc_control(upipe_ts_demux->uref_mgr);
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
 * @param pid PID
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
 * @param psi_pid psi_pid structure
 */
static void
    upipe_ts_demux_psi_pid_release(struct upipe_ts_demux_psi_pid *psi_pid)
{
    assert(psi_pid != NULL);

    psi_pid->refcount--;
    if (!psi_pid->refcount) {
        ulist_delete(upipe_ts_demux_psi_pid_to_uchain(psi_pid));
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
        uref_free(flow_def);
        return true;
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

/** @internal @This builds the output flow def.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_program_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        upipe_ts_demux_program_from_upipe(upipe);
    struct uref *flow_def_pmt;
    if (!ubase_check(upipe_get_flow_def(upipe_ts_demux_program->pmtd,
                                        &flow_def_pmt)) || flow_def_pmt == NULL)
        return;

    struct uref *flow_def = NULL;
    if (upipe_ts_demux_program->eitd != NULL &&
        ubase_check(upipe_get_flow_def(upipe_ts_demux_program->eitd,
                                       &flow_def)) && flow_def != NULL) {
        flow_def = uref_dup(flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uref_attr_import(flow_def, flow_def_pmt);
    } else {
        flow_def = uref_dup(flow_def_pmt);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
    }

    upipe_ts_demux_program_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_demux_program_output(upipe, NULL, NULL);
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
    struct upipe_ts_demux *demux = upipe_ts_demux_from_program_mgr(upipe->mgr);
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);

    struct uref *flow_def = va_arg(args, struct uref *);
    UBASE_ALLOC_RETURN(flow_def);
    uint64_t pmtd_pcrpid;

    UBASE_RETURN(uref_ts_flow_get_pcr_pid(flow_def, &pmtd_pcrpid))
    if (upipe_ts_demux_program->pcr_pid != pmtd_pcrpid) {
        if (upipe_ts_demux_program->pcr_split_output != NULL) {
            upipe_release(upipe_ts_demux_program->pcr_split_output);
            upipe_ts_demux_program->pcr_split_output = NULL;
        }

        upipe_ts_demux_program->pcr_pid = pmtd_pcrpid;
        upipe_ts_demux_program_check_pcr(upipe);
    }

    upipe_ts_demux_program_build_flow_def(upipe);

    if (!ubase_check(uref_ts_flow_get_eit(flow_def))) {
        if (upipe_ts_demux_program->psi_split_output_eit != NULL) {
            upipe_release(upipe_ts_demux_program->psi_split_output_eit);
            upipe_ts_demux_psi_pid_release(upipe_ts_demux_program->psi_pid_eit);
            upipe_ts_demux_program->psi_split_output_eit = NULL;
        }
        upipe_release(upipe_ts_demux_program->eitd);
        upipe_ts_demux_program->eitd = NULL;
        return UBASE_ERR_NONE;
    }

    if (upipe_ts_demux_program->eitd != NULL)
        return UBASE_ERR_NONE;

    /* get psi_split inner pipe */
    upipe_ts_demux_program->psi_pid_eit =
        upipe_ts_demux_psi_pid_use(upipe_ts_demux_to_upipe(demux), EIT_PID);
    if (unlikely(upipe_ts_demux_program->psi_pid_eit == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    /* set filter on table 0x4e, current, sid */
    uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
    uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
    memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
    memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
    psi_set_syntax(filter);
    psi_set_syntax(mask);
    psi_set_tableid(filter, EIT_TABLE_ID_PF_ACTUAL);
    psi_set_tableid(mask, 0xff);
    psi_set_current(filter);
    psi_set_current(mask);
    eit_set_sid(filter, upipe_ts_demux_program->program);
    eit_set_sid(mask, 0xffff);
    flow_def = uref_alloc_control(demux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegtspsi.mpegtseit.")) ||
                 !ubase_check(uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE_SYNTAX1)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, EIT_PID)) ||
                 (upipe_ts_demux_program->psi_split_output_eit =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux_program->psi_pid_eit->psi_split,
                          uprobe_pfx_alloc(
                              uprobe_use(&upipe_ts_demux_program->proxy_probe),
                              UPROBE_LOG_VERBOSE, "psi_split output eit"),
                          flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    uref_free(flow_def);

    /* allocate EIT decoder */
    upipe_ts_demux_program->eitd =
        upipe_void_alloc_output(upipe_ts_demux_program->psi_split_output_eit,
                   ts_demux_mgr->ts_eitd_mgr,
                   uprobe_pfx_alloc(
                       uprobe_use(&upipe_ts_demux_program->eitd_probe),
                       UPROBE_LOG_VERBOSE, "eitd"));
    if (unlikely(upipe_ts_demux_program->eitd == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

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

    /* send the event upstream */
    upipe_split_throw_update(upipe);

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
        case UPROBE_NEED_OUTPUT:
            return UBASE_ERR_NONE;
        case UPROBE_SPLIT_UPDATE:
            return upipe_ts_demux_program_pmtd_update(upipe, pmtd, event, args);
        default:
            return upipe_throw_proxy(upipe, pmtd, event, args);
    }
}

/** @internal @This catches events coming from eitd inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux_program
 * @param eitd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_eitd_probe(struct uprobe *uprobe,
                                             struct upipe *eitd,
                                             int event, va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, eitd_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);

    switch (event) {
        case UPROBE_NEW_FLOW_DEF:
            upipe_ts_demux_program_build_flow_def(upipe);
            return UBASE_ERR_NONE;
        case UPROBE_NEED_OUTPUT:
            return UBASE_ERR_NONE;
        default:
            return upipe_throw_proxy(upipe, eitd, event, args);
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

/** @internal @This catches events coming from other inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_program_proxy_probe(struct uprobe *uprobe,
                                              struct upipe *inner,
                                              int event, va_list args)
{
    struct upipe_ts_demux_program *upipe_ts_demux_program =
        container_of(uprobe, struct upipe_ts_demux_program, proxy_probe);
    struct upipe *upipe =
        upipe_ts_demux_program_to_upipe(upipe_ts_demux_program);
    return upipe_throw_proxy(upipe, inner, event, args);
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

    struct uref *flow_def = uref_alloc_control(demux->uref_mgr);
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
                         uprobe_use(&demux->proxy_probe),
                         UPROBE_LOG_VERBOSE, "split output PCR %"PRIu16,
                         upipe_ts_demux_program->pcr_pid),
                     flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_program->pcr_split_output == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    struct upipe *decaps =
        upipe_void_alloc_output(upipe_ts_demux_program->pcr_split_output,
                           ts_demux_mgr->ts_decaps_mgr,
                           uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_demux_program->pcr_probe),
                             UPROBE_LOG_VERBOSE,
                             "decaps PCR %"PRIu16,
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
    upipe_ts_demux_program_init_output(upipe);
    upipe_ts_demux_program_init_output_mgr(upipe);
    upipe_ts_demux_program_init_sub_outputs(upipe);
    upipe_ts_demux_program->flow_def_input = flow_def;
    upipe_ts_demux_program->program = 0;
    upipe_ts_demux_program->pmt_rap = 0;
    upipe_ts_demux_program->pcr_pid = 0;
    upipe_ts_demux_program->pcr_split_output = NULL;
    upipe_ts_demux_program->psi_pid_pmt =
        upipe_ts_demux_program->psi_pid_eit = NULL;
    upipe_ts_demux_program->psi_split_output_pmt =
        upipe_ts_demux_program->psi_split_output_eit = NULL;
    upipe_ts_demux_program->pmtd = upipe_ts_demux_program->eitd = NULL;
    upipe_ts_demux_program->timestamp_offset = 0;
    upipe_ts_demux_program->timestamp_highest = TS_CLOCK_MAX;
    upipe_ts_demux_program->last_pcr = TS_CLOCK_MAX;
    uprobe_init(&upipe_ts_demux_program->pmtd_probe,
                upipe_ts_demux_program_pmtd_probe, NULL);
    upipe_ts_demux_program->pmtd_probe.refcount =
        upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program);
    uprobe_init(&upipe_ts_demux_program->eitd_probe,
                upipe_ts_demux_program_eitd_probe, NULL);
    upipe_ts_demux_program->eitd_probe.refcount =
        upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program);
    uprobe_init(&upipe_ts_demux_program->pcr_probe,
                upipe_ts_demux_program_pcr_probe, NULL);
    upipe_ts_demux_program->pcr_probe.refcount =
        upipe_ts_demux_program_to_urefcount_real(upipe_ts_demux_program);
    uprobe_init(&upipe_ts_demux_program->proxy_probe,
                upipe_ts_demux_program_proxy_probe, NULL);
    upipe_ts_demux_program->proxy_probe.refcount =
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
                 (upipe_ts_demux_program->psi_pid_pmt =
                      upipe_ts_demux_psi_pid_use(upipe_ts_demux_to_upipe(demux),
                          upipe_ts_demux_program->pmt_pid)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    upipe_ts_demux_program->psi_split_output_pmt =
        upipe_flow_alloc_sub(upipe_ts_demux_program->psi_pid_pmt->psi_split,
                             uprobe_pfx_alloc(
                                 uprobe_use(&upipe_ts_demux_program->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "psi_split output"),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_demux_program->psi_split_output_pmt == NULL)) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_program->psi_pid_pmt);
        upipe_ts_demux_program->psi_pid_pmt = NULL;
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe_ts_demux_to_upipe(demux)->mgr);
    upipe_ts_demux_program->pmtd =
        upipe_void_alloc_output(upipe_ts_demux_program->psi_split_output_pmt,
                ts_demux_mgr->ts_pmtd_mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_ts_demux_program->pmtd_probe),
                    UPROBE_LOG_VERBOSE, "pmtd"));
    if (unlikely(upipe_ts_demux_program->pmtd == NULL)) {
        upipe_release(upipe_ts_demux_program->psi_split_output_pmt);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_program->psi_pid_pmt);
        upipe_ts_demux_program->psi_pid_pmt = NULL;
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
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_demux_program_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_program_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_demux_program_set_output(upipe, output);
        }
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
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            struct upipe_ts_demux_program *upipe_ts_demux_program =
                upipe_ts_demux_program_from_upipe(upipe);
            if (upipe_ts_demux_program->pmtd == NULL)
                return UBASE_ERR_UNHANDLED;
            return upipe_split_iterate(upipe_ts_demux_program->pmtd, p);
        }

        default:
            return UBASE_ERR_NONE;
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

    uprobe_clean(&upipe_ts_demux_program->pmtd_probe);
    uprobe_clean(&upipe_ts_demux_program->eitd_probe);
    uprobe_clean(&upipe_ts_demux_program->pcr_probe);
    uprobe_clean(&upipe_ts_demux_program->proxy_probe);
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
    upipe_ts_demux_program_throw_sub_outputs(upipe, UPROBE_SOURCE_END);
    /* close PMT to release ESs */
    if (upipe_ts_demux_program->psi_split_output_pmt != NULL) {
        upipe_release(upipe_ts_demux_program->psi_split_output_pmt);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_program->psi_pid_pmt);
        upipe_ts_demux_program->psi_split_output_pmt = NULL;
    }
    upipe_release(upipe_ts_demux_program->pmtd);
    upipe_ts_demux_program->pmtd = NULL;
    if (upipe_ts_demux_program->psi_split_output_eit != NULL) {
        upipe_release(upipe_ts_demux_program->psi_split_output_eit);
        upipe_ts_demux_psi_pid_release(upipe_ts_demux_program->psi_pid_eit);
        upipe_ts_demux_program->psi_split_output_eit = NULL;
    }
    upipe_release(upipe_ts_demux_program->eitd);
    upipe_ts_demux_program->eitd = NULL;
    upipe_split_throw_update(upipe);

    if (upipe_ts_demux_program->pcr_split_output != NULL)
        upipe_release(upipe_ts_demux_program->pcr_split_output);
    upipe_ts_demux_program_clean_output(upipe);
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

/** @internal @This cleans up the list of programs (from the PAT).
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_clean_pat_programs(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_demux->pat_programs, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(uref);
    }
}

/** @internal @This builds up the list of programs, from the PAT and SDT.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_ts_demux_build_pat_programs(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    upipe_ts_demux_clean_pat_programs(upipe);
    if (unlikely(upipe_ts_demux->patd == NULL))
        return UBASE_ERR_NONE;

    struct uref *program = NULL;
    while (ubase_check(upipe_split_iterate(upipe_ts_demux->patd, &program)) &&
           program != NULL) {
        uint64_t program_number;
        if (unlikely(!ubase_check(uref_flow_get_id(program, &program_number))))
            continue;

        struct uref *uref = NULL;
        if (upipe_ts_demux->sdtd != NULL) {
            struct uref *service = NULL;
            while (ubase_check(upipe_split_iterate(upipe_ts_demux->sdtd,
                                                   &service)) &&
                   service != NULL) {
                uint64_t service_id;
                if (!ubase_check(uref_flow_get_id(service, &service_id)) ||
                    service_id != program_number)
                    continue;

                uref = uref_dup(service);
                if (likely(uref != NULL))
                    uref_attr_import(uref, program);
                break;
            }
        }
        if (uref == NULL) {
            uref = uref_dup(program);
            if (unlikely(uref == NULL))
                continue;
        }
        ulist_add(&upipe_ts_demux->pat_programs, uref_to_uchain(uref));

        /* send set_flow_def on the program */
        uref = uref_dup(uref);
        const char *def;
        if (!ubase_check(uref_flow_get_raw_def(uref, &def)) ||
            !ubase_check(uref_flow_set_def(uref, def)) ||
            !ubase_check(uref_flow_delete_raw_def(uref))) {
            uref_free(uref);
            continue;
        }

        struct uchain *uchain;
        ulist_foreach (&upipe_ts_demux->programs, uchain) {
            struct upipe_ts_demux_program *upipe_ts_demux_program =
                upipe_ts_demux_program_from_uchain(uchain);
            if (upipe_ts_demux_program->program == program_number) {
                upipe_set_flow_def(upipe_ts_demux_program->pmtd, uref);
                break;
            }
        }
        uref_free(uref);
    }

    /* send the event upstream */
    return upipe_split_throw_update(upipe);
}

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

/** @internal @This updates the NIT decoder.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_update_nit(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->conformance != UPIPE_TS_CONFORMANCE_DVB) {
        if (upipe_ts_demux->psi_split_output_nit != NULL) {
            upipe_release(upipe_ts_demux->psi_split_output_nit);
            upipe_ts_demux->psi_split_output_nit = NULL;
        }
        if (upipe_ts_demux->psi_pid_nit != NULL) {
            upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_nit);
            upipe_ts_demux->psi_pid_nit = NULL;
        }
        upipe_release(upipe_ts_demux->nitd);
        upipe_ts_demux->nitd = NULL;
        return;
    }

    if (upipe_ts_demux->psi_pid_nit != NULL)
        return;

    /* get psi_split inner pipe */
    upipe_ts_demux->psi_pid_nit = upipe_ts_demux_psi_pid_use(upipe, NIT_PID);
    if (unlikely(upipe_ts_demux->psi_pid_nit == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* set filter on table 0x40, current */
    uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
    uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
    memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
    memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
    psi_set_syntax(filter);
    psi_set_syntax(mask);
    psi_set_tableid(filter, NIT_TABLE_ID_ACTUAL);
    psi_set_tableid(mask, 0xff);
    psi_set_current(filter);
    psi_set_current(mask);
    struct uref *flow_def = uref_alloc_control(upipe_ts_demux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegtspsi.mpegtsnit.")) ||
                 !ubase_check(uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE_SYNTAX1)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, NIT_PID)) ||
                 (upipe_ts_demux->psi_split_output_nit =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux->psi_pid_nit->psi_split,
                          uprobe_pfx_alloc(
                              uprobe_use(&upipe_ts_demux->proxy_probe),
                              UPROBE_LOG_VERBOSE, "psi_split output nit"),
                          flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_free(flow_def);

    /* allocate NIT decoder */
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    upipe_ts_demux->nitd =
        upipe_void_alloc_output(upipe_ts_demux->psi_split_output_nit,
                   ts_demux_mgr->ts_nitd_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->nitd_probe),
                                    UPROBE_LOG_VERBOSE, "nitd"));
    if (unlikely(upipe_ts_demux->nitd == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
}

/** @internal @This updates the SDT decoder.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_update_sdt(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->conformance != UPIPE_TS_CONFORMANCE_DVB) {
        if (upipe_ts_demux->psi_split_output_sdt != NULL) {
            upipe_release(upipe_ts_demux->psi_split_output_sdt);
            upipe_ts_demux->psi_split_output_sdt = NULL;
        }
        if (upipe_ts_demux->psi_pid_sdt != NULL) {
            upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_sdt);
            upipe_ts_demux->psi_pid_sdt = NULL;
        }
        upipe_release(upipe_ts_demux->sdtd);
        upipe_ts_demux->sdtd = NULL;
        return;
    }

    if (upipe_ts_demux->psi_pid_sdt != NULL)
        return;

    /* get psi_split inner pipe */
    upipe_ts_demux->psi_pid_sdt = upipe_ts_demux_psi_pid_use(upipe, SDT_PID);
    if (unlikely(upipe_ts_demux->psi_pid_sdt == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* set filter on table 0x42, current */
    uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
    uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
    memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
    memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
    psi_set_syntax(filter);
    psi_set_syntax(mask);
    psi_set_tableid(filter, SDT_TABLE_ID_ACTUAL);
    psi_set_tableid(mask, 0xff);
    psi_set_current(filter);
    psi_set_current(mask);
    struct uref *flow_def = uref_alloc_control(upipe_ts_demux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegtspsi.mpegtssdt.")) ||
                 !ubase_check(uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE_SYNTAX1)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, SDT_PID)) ||
                 (upipe_ts_demux->psi_split_output_sdt =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux->psi_pid_sdt->psi_split,
                          uprobe_pfx_alloc(
                              uprobe_use(&upipe_ts_demux->proxy_probe),
                              UPROBE_LOG_VERBOSE, "psi_split output sdt"),
                          flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_free(flow_def);

    /* allocate SDT decoder */
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    upipe_ts_demux->sdtd =
        upipe_void_alloc_output(upipe_ts_demux->psi_split_output_sdt,
                   ts_demux_mgr->ts_sdtd_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->sdtd_probe),
                                    UPROBE_LOG_VERBOSE, "sdtd"));
    if (unlikely(upipe_ts_demux->sdtd == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
}

/** @internal @This updates the TDT decoder.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_update_tdt(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->conformance != UPIPE_TS_CONFORMANCE_DVB) {
        if (upipe_ts_demux->psi_split_output_tdt != NULL) {
            upipe_release(upipe_ts_demux->psi_split_output_tdt);
            upipe_ts_demux->psi_split_output_tdt = NULL;
        }
        if (upipe_ts_demux->psi_pid_tdt != NULL) {
            upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_tdt);
            upipe_ts_demux->psi_pid_tdt = NULL;
        }
        upipe_release(upipe_ts_demux->tdtd);
        upipe_ts_demux->tdtd = NULL;
        return;
    }

    if (upipe_ts_demux->psi_pid_tdt != NULL)
        return;

    /* get psi_split inner pipe */
    upipe_ts_demux->psi_pid_tdt = upipe_ts_demux_psi_pid_use(upipe, TDT_PID);
    if (unlikely(upipe_ts_demux->psi_pid_tdt == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* set filter on table 0x70 */
    uint8_t filter[PSI_HEADER_SIZE];
    uint8_t mask[PSI_HEADER_SIZE];
    memset(filter, 0, PSI_HEADER_SIZE);
    memset(mask, 0, PSI_HEADER_SIZE);
    psi_set_syntax(mask);
    psi_set_tableid(filter, TDT_TABLE_ID);
    psi_set_tableid(mask, 0xff);
    struct uref *flow_def = uref_alloc_control(upipe_ts_demux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "block.mpegtspsi.mpegtstdt.")) ||
                 !ubase_check(uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                              PSI_HEADER_SIZE)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, TDT_PID)) ||
                 (upipe_ts_demux->psi_split_output_tdt =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux->psi_pid_tdt->psi_split,
                          uprobe_pfx_alloc(
                              uprobe_use(&upipe_ts_demux->proxy_probe),
                              UPROBE_LOG_VERBOSE, "psi_split output tdt"),
                          flow_def)) == NULL)) {
        if (flow_def != NULL)
            uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_free(flow_def);

    /* allocate TDT decoder */
    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    upipe_ts_demux->tdtd =
        upipe_void_alloc_output(upipe_ts_demux->psi_split_output_tdt,
                   ts_demux_mgr->ts_tdtd_mgr,
                   uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->proxy_probe),
                                    UPROBE_LOG_VERBOSE, "tdtd"));
    if (unlikely(upipe_ts_demux->tdtd == NULL))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
}

/** @internal @This builds the output flow def.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_demux_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    struct uref *flow_def_pat;
    if (upipe_ts_demux->patd == NULL ||
        !ubase_check(upipe_get_flow_def(upipe_ts_demux->patd, &flow_def_pat)) ||
        flow_def_pat == NULL)
        return;
    uint64_t tsid = UINT64_MAX;
    uref_flow_get_id(flow_def_pat, &tsid);

    struct uref *flow_def_sdt = NULL;
    if (upipe_ts_demux->sdtd != NULL)
        upipe_get_flow_def(upipe_ts_demux->sdtd, &flow_def_sdt);
    if (flow_def_sdt != NULL) {
        uint64_t sdt_tsid;
        if (unlikely(tsid != UINT64_MAX &&
                     uref_flow_get_id(flow_def_sdt, &sdt_tsid) &&
                     tsid != sdt_tsid))
        upipe_warn_va(upipe,
                "TSID mismatch between PAT (%"PRIu64") and SDT (%"PRIu64")",
                tsid, sdt_tsid);
    }

    struct uref *flow_def = NULL;
    if (upipe_ts_demux->nitd != NULL &&
        ubase_check(upipe_get_flow_def(upipe_ts_demux->nitd, &flow_def)) &&
        flow_def != NULL) {
        flow_def = uref_dup(flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        if (flow_def_sdt != NULL)
            uref_attr_import(flow_def, flow_def_sdt);
        uref_attr_import(flow_def, flow_def_pat);

    } else if (flow_def_sdt != NULL) {
        flow_def = uref_dup(flow_def_sdt);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uref_attr_import(flow_def, flow_def_pat);

    } else {
        flow_def = uref_dup(flow_def_pat);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
    }

    upipe_ts_conformance_to_flow_def(flow_def, upipe_ts_demux->conformance);

    upipe_ts_demux_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_demux_output(upipe, NULL, NULL);
}

/** @internal @This changes the current conformance, and start necessary
 * decoders.
 *
 * @param upipe description structure of the pipe
 * @param conformance new conformance
 */
static void upipe_ts_demux_conformance_change(struct upipe *upipe,
        enum upipe_ts_conformance conformance)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    if (upipe_ts_demux->conformance == conformance)
        return;

    upipe_notice_va(upipe, "changing conformance to %s",
                    upipe_ts_conformance_print(conformance));
    upipe_ts_demux->conformance = conformance;

    upipe_ts_demux_build_flow_def(upipe);
    upipe_ts_demux_update_nit(upipe);
    upipe_ts_demux_update_sdt(upipe);
    upipe_ts_demux_update_tdt(upipe);
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
            /* No idea what this is */
            upipe_ts_demux_conformance_change(upipe, UPIPE_TS_CONFORMANCE_ISO);
            break;
        case 0:
            /* No NIT yet, assume DVB conformance without tables */
            upipe_ts_demux_conformance_change(upipe,
                    UPIPE_TS_CONFORMANCE_DVB_NO_TABLES);
            break;
        case 16:
            /* Mandatory PID in DVB systems - could also be ISDB */
            upipe_ts_demux_conformance_change(upipe, UPIPE_TS_CONFORMANCE_DVB);
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
    UBASE_ALLOC_RETURN(uref);

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
               flow_def != NULL)
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

    return upipe_ts_demux_build_pat_programs(upipe);
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
        case UPROBE_NEW_FLOW_DEF:
            upipe_ts_demux_build_flow_def(upipe);
            return UBASE_ERR_NONE;
        case UPROBE_NEED_OUTPUT:
            return UBASE_ERR_NONE;
        case UPROBE_NEW_RAP:
            return upipe_ts_demux_patd_new_rap(upipe, patd, event, args);
        case UPROBE_SPLIT_UPDATE:
            return upipe_ts_demux_patd_update(upipe, patd, event, args);
        default:
            return upipe_throw_proxy(upipe, patd, event, args);
    }
}

/** @internal @This catches events coming from nitd inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param nitd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_nitd_probe(struct uprobe *uprobe,
                                     struct upipe *nitd,
                                     int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, nitd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_NEW_FLOW_DEF:
            upipe_ts_demux_build_flow_def(upipe);
            return UBASE_ERR_NONE;
        case UPROBE_NEED_OUTPUT:
            return UBASE_ERR_NONE;
        default:
            return upipe_throw_proxy(upipe, nitd, event, args);
    }
}

/** @internal @This catches events coming from sdtd inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_demux
 * @param sdtd pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_demux_sdtd_probe(struct uprobe *uprobe,
                                     struct upipe *sdtd,
                                     int event, va_list args)
{
    struct upipe_ts_demux *upipe_ts_demux =
        container_of(uprobe, struct upipe_ts_demux, sdtd_probe);
    struct upipe *upipe = upipe_ts_demux_to_upipe(upipe_ts_demux);

    switch (event) {
        case UPROBE_NEW_FLOW_DEF:
            upipe_ts_demux_build_flow_def(upipe);
            return UBASE_ERR_NONE;
        case UPROBE_NEED_OUTPUT:
            return UBASE_ERR_NONE;
        case UPROBE_SPLIT_UPDATE:
            return upipe_ts_demux_build_pat_programs(upipe);
        default:
            return upipe_throw_proxy(upipe, sdtd, event, args);
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
    upipe_ts_demux_init_output(upipe);
    upipe_ts_demux_init_uref_mgr(upipe);
    upipe_ts_demux_init_program_mgr(upipe);
    upipe_ts_demux_init_sub_programs(upipe);

    upipe_ts_demux_init_sync(upipe);
    upipe_ts_demux->input = upipe_ts_demux->split = upipe_ts_demux->setrap =
        upipe_ts_demux->null = NULL;
    upipe_ts_demux->psi_split_output_pat = upipe_ts_demux->patd = NULL;
    upipe_ts_demux->psi_split_output_nit = upipe_ts_demux->nitd = NULL;
    upipe_ts_demux->psi_split_output_sdt = upipe_ts_demux->sdtd = NULL;
    upipe_ts_demux->psi_split_output_tdt = upipe_ts_demux->tdtd = NULL;
    upipe_ts_demux->psi_pid_pat = upipe_ts_demux->psi_pid_nit =
        upipe_ts_demux->psi_pid_sdt = upipe_ts_demux->psi_pid_tdt = NULL;
    ulist_init(&upipe_ts_demux->pat_programs);

    ulist_init(&upipe_ts_demux->psi_pids);
    upipe_ts_demux->conformance = UPIPE_TS_CONFORMANCE_DVB_NO_TABLES;
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
    uprobe_init(&upipe_ts_demux->patd_probe, upipe_ts_demux_patd_probe, NULL);
    upipe_ts_demux->patd_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->nitd_probe, upipe_ts_demux_nitd_probe, NULL);
    upipe_ts_demux->nitd_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->sdtd_probe, upipe_ts_demux_sdtd_probe, NULL);
    upipe_ts_demux->sdtd_probe.refcount =
        upipe_ts_demux_to_urefcount_real(upipe_ts_demux);
    uprobe_init(&upipe_ts_demux->input_probe, upipe_ts_demux_input_probe, NULL);
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
    upipe_ts_demux_demand_uref_mgr(upipe);

    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
    if (unlikely(upipe_ts_demux->uref_mgr == NULL ||
                 (upipe_ts_demux->setrap =
                    upipe_void_alloc(ts_demux_mgr->setrap_mgr,
                        uprobe_pfx_alloc(
                            uprobe_use(&upipe_ts_demux->proxy_probe),
                            UPROBE_LOG_VERBOSE, "setrap"))) == NULL ||
                 (upipe_ts_demux->split =
                    upipe_void_alloc_output(upipe_ts_demux->setrap,
                        ts_demux_mgr->ts_split_mgr,
                        uprobe_pfx_alloc(
                            uprobe_use(&upipe_ts_demux->split_probe),
                            UPROBE_LOG_VERBOSE, "split"))) == NULL ||
                 (upipe_ts_demux->null =
                    upipe_void_alloc(ts_demux_mgr->null_mgr,
                        uprobe_pfx_alloc(
                            uprobe_use(&upipe_ts_demux->proxy_probe),
                            UPROBE_LOG_NOTICE, "null"))) == NULL ||
                 (upipe_ts_demux->psi_pid_pat =
                    upipe_ts_demux_psi_pid_use(upipe, PAT_PID)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
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
    struct uref *flow_def = uref_alloc_control(upipe_ts_demux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def,
                         "block.mpegtspsi.mpegtspat.")) ||
                 !ubase_check(uref_ts_flow_set_psi_filter(flow_def,
                         filter, mask, PSI_HEADER_SIZE_SYNTAX1)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, PAT_PID)) ||
                 (upipe_ts_demux->psi_split_output_pat =
                      upipe_flow_alloc_sub(
                          upipe_ts_demux->psi_pid_pat->psi_split,
                          uprobe_pfx_alloc(
                              uprobe_use(&upipe_ts_demux->proxy_probe),
                              UPROBE_LOG_VERBOSE, "psi_split output pat"),
                          flow_def)) == NULL ||
                 (upipe_ts_demux->patd =
                  upipe_void_alloc_output(upipe_ts_demux->psi_split_output_pat,
                      ts_demux_mgr->ts_patd_mgr,
                      uprobe_pfx_alloc(uprobe_use(&upipe_ts_demux->patd_probe),
                                       UPROBE_LOG_VERBOSE, "patd"))) == NULL)) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }
    uref_free(flow_def);

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
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;
    uref_free(upipe_ts_demux->flow_def_input);
    upipe_ts_demux->flow_def_input = flow_def_dup;

    struct upipe_ts_demux_mgr *ts_demux_mgr =
        upipe_ts_demux_mgr_from_upipe_mgr(upipe->mgr);
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
        upipe_ts_demux_store_first_inner(upipe, input);
        upipe_set_output(input, upipe_ts_demux->setrap);

    } else {
        upipe_ts_demux_store_first_inner(upipe,
                                         upipe_use(upipe_ts_demux->setrap));
        upipe_ts_demux_sync_acquired(upipe);
    }

    return upipe_set_flow_def(upipe_ts_demux->input, flow_def);
}

/** @internal @This iterates over flow definition.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return an error code
 */
static int upipe_ts_demux_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_demux *upipe_ts_demux = upipe_ts_demux_from_upipe(upipe);
    assert(p != NULL);
    struct uchain *uchain;
    if (*p != NULL)
        uchain = uref_to_uchain(*p);
    else
        uchain = &upipe_ts_demux->pat_programs;
    if (ulist_is_last(&upipe_ts_demux->pat_programs, uchain)) {
        *p = NULL;
        return UBASE_ERR_NONE;
    }
    *p = uref_from_uchain(uchain->next);
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
        case UPIPE_TS_CONFORMANCE_DVB_NO_TABLES:
        case UPIPE_TS_CONFORMANCE_ATSC:
        case UPIPE_TS_CONFORMANCE_ISDB:
            upipe_ts_demux->auto_conformance = false;
            upipe_ts_demux_conformance_change(upipe, conformance);
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
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_demux_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_demux_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_demux_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_demux_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_demux_iterate_sub(upipe, p);
        }
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_demux_iterate(upipe, p);
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

    return upipe_ts_demux_control_bin_input(upipe, command, args);
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
    uprobe_clean(&upipe_ts_demux->nitd_probe);
    uprobe_clean(&upipe_ts_demux->patd_probe);
    uprobe_clean(&upipe_ts_demux->sdtd_probe);
    uprobe_clean(&upipe_ts_demux->input_probe);
    uprobe_clean(&upipe_ts_demux->split_probe);
    uref_free(upipe_ts_demux->flow_def_input);
    upipe_ts_demux_clean_sub_programs(upipe);
    upipe_ts_demux_clean_sync(upipe);
    upipe_ts_demux_clean_uref_mgr(upipe);
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
    /* release the packet blocked in ts_sync */
    upipe_ts_demux_store_first_inner(upipe, NULL);

    upipe_ts_demux_throw_sub_programs(upipe, UPROBE_SOURCE_END);

    /* close PAT to release programs */
    if (upipe_ts_demux->psi_split_output_pat != NULL) {
        upipe_release(upipe_ts_demux->psi_split_output_pat);
        upipe_ts_demux->psi_split_output_pat = NULL;
    }
    if (upipe_ts_demux->psi_pid_pat != NULL) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_pat);
        upipe_ts_demux->psi_pid_pat = NULL;
    }
    upipe_release(upipe_ts_demux->patd);
    upipe_ts_demux->patd = NULL;

    /* close NIT */
    if (upipe_ts_demux->psi_split_output_nit != NULL) {
        upipe_release(upipe_ts_demux->psi_split_output_nit);
        upipe_ts_demux->psi_split_output_nit = NULL;
    }
    if (upipe_ts_demux->psi_pid_nit != NULL) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_nit);
        upipe_ts_demux->psi_pid_nit = NULL;
    }
    upipe_release(upipe_ts_demux->nitd);
    upipe_ts_demux->nitd = NULL;

    /* close SDT */
    if (upipe_ts_demux->psi_split_output_sdt != NULL) {
        upipe_release(upipe_ts_demux->psi_split_output_sdt);
        upipe_ts_demux->psi_split_output_sdt = NULL;
    }
    if (upipe_ts_demux->psi_pid_sdt != NULL) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_sdt);
        upipe_ts_demux->psi_pid_sdt = NULL;
    }
    upipe_release(upipe_ts_demux->sdtd);
    upipe_ts_demux->sdtd = NULL;

    /* close TDT */
    if (upipe_ts_demux->psi_split_output_tdt != NULL) {
        upipe_release(upipe_ts_demux->psi_split_output_tdt);
        upipe_ts_demux->psi_split_output_tdt = NULL;
    }
    if (upipe_ts_demux->psi_pid_tdt != NULL) {
        upipe_ts_demux_psi_pid_release(upipe_ts_demux->psi_pid_tdt);
        upipe_ts_demux->psi_pid_tdt = NULL;
    }
    upipe_release(upipe_ts_demux->tdtd);
    upipe_ts_demux->tdtd = NULL;

    upipe_ts_demux_clean_pat_programs(upipe);
    upipe_split_throw_update(upipe);

    if (upipe_ts_demux->split != NULL)
        upipe_release(upipe_ts_demux->split);
    if (upipe_ts_demux->setrap != NULL)
        upipe_release(upipe_ts_demux->setrap);
    if (upipe_ts_demux->null != NULL)
        upipe_release(upipe_ts_demux->null);
    upipe_ts_demux_clean_bin_input(upipe);
    upipe_ts_demux_clean_output(upipe);
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
    upipe_mgr_release(ts_demux_mgr->null_mgr);
    upipe_mgr_release(ts_demux_mgr->setrap_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_split_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_sync_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_check_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_decaps_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_psim_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_psi_split_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_patd_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_nitd_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_sdtd_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_tdtd_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_pmtd_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_eitd_mgr);
    upipe_mgr_release(ts_demux_mgr->ts_pesd_mgr);
    upipe_mgr_release(ts_demux_mgr->mpgaf_mgr);
    upipe_mgr_release(ts_demux_mgr->a52f_mgr);
    upipe_mgr_release(ts_demux_mgr->mpgvf_mgr);
    upipe_mgr_release(ts_demux_mgr->h264f_mgr);
    upipe_mgr_release(ts_demux_mgr->telxf_mgr);
    upipe_mgr_release(ts_demux_mgr->dvbsubf_mgr);
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
        GET_SET_MGR(ts_nitd, TS_NITD)
        GET_SET_MGR(ts_sdtd, TS_SDTD)
        GET_SET_MGR(ts_tdtd, TS_TDTD)
        GET_SET_MGR(ts_pmtd, TS_PMTD)
        GET_SET_MGR(ts_eitd, TS_EITD)
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
    ts_demux_mgr->ts_nitd_mgr = upipe_ts_nitd_mgr_alloc();
    ts_demux_mgr->ts_sdtd_mgr = upipe_ts_sdtd_mgr_alloc();
    ts_demux_mgr->ts_tdtd_mgr = upipe_ts_tdtd_mgr_alloc();
    ts_demux_mgr->ts_pmtd_mgr = upipe_ts_pmtd_mgr_alloc();
    ts_demux_mgr->ts_eitd_mgr = upipe_ts_eitd_mgr_alloc();
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

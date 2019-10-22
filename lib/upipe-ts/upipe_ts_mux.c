/*
 * Copyright (C) 2013-2019 OpenHeadend S.A.R.L.
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
 * @short Upipe higher-level module muxing elementary streams in a TS
 * Four parts in this file:
 * @list
 * @item psi_pid structure, which handles PSI multiplexing using ts_psi_join
 * @item input sink pipe, which is returned to the application, and
 * represents an elementary stream; it sets up the ts_encaps inner pipe
 * @item program join pipe, which is returned to the application, and
 * represents a program
 * @item mux source pipe which sets up the ts_psig, and ts_sig inner pipes
 *
 * Normative references:
 *  - ISO/IEC 13818-1:2007(E) (MPEG-2 Systems)
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 *  - ETSI TS 103 197 V1.3.1 (2003-01) (DVB SimulCrypt)
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_program_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/urefcount_helper.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-framers/uref_h265_flow.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-framers/uref_mpga_flow.h>
#include <upipe-framers/uref_mpgv_flow.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/upipe_ts_psi_join.h>
#include <upipe-ts/upipe_ts_psi_generator.h>
#include <upipe-ts/upipe_ts_si_generator.h>
#include <upipe-ts/upipe_ts_scte35_generator.h>
#include <upipe-ts/upipe_ts_tstd.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/pes.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** maximum number of SIDs */
#define MAX_SIDS UINT16_MAX
/** maximum number of PIDs */
#define MAX_PIDS 8192
/** undefined PCR PID */
#define UNDEF_PCR 8191
/** PCR tolerance in ppm */
#define PCR_TOLERANCE_PPM 30
/** default muxing delay */
#define DEFAULT_MUX_DELAY (UCLOCK_FREQ / 100)
/** minimum allowed buffering (== max decoder VBV) */
#define MIN_BUFFERING (UCLOCK_FREQ * 10)
/** number of packets to output in one iteration of the live mode */
#define NB_PACKETS 7
/** T-STD TB octet rate for PSI tables */
#define TB_RATE_PSI 125000
/** T-STD TB octet rate for misc audio */
#define TB_RATE_AUDIO 250000
/** T-STD TB octet rate for teletext */
#define TB_RATE_TELX 843750
/** T-STD TS buffer */
#define T_STD_TS_BUFFER 512
/** A/52 buffer size */
#define BS_A52 5696
/** ADTS buffer size for <= 2 channels */
#define BS_ADTS_2 3584
/** ADTS buffer size for <= 8 channels */
#define BS_ADTS_8 8976
/** ADTS buffer size for <= 12 channels */
#define BS_ADTS_12 12804
/** ADTS buffer size for <= 48 channels */
#define BS_ADTS_48 51216
/** Teletext buffer size */
#define BS_TELX 1504
/** DVB subtitles buffer size (ETSI EN 300 743 5.) */
#define BS_DVBSUB 24576
/** max retention time for most streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY UCLOCK_FREQ
/** max retention time for ISO/IEC 14496 streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_14496 (UCLOCK_FREQ * 10)
/** max retention time for HEVC streams (FIXME) */
#define MAX_DELAY_HEVC (UCLOCK_FREQ * 10)
/** max retention time for still pictures streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_STILL (UCLOCK_FREQ * 60)
/** max retention time for teletext (ETSI EN 300 472 5.) */
#define MAX_DELAY_TELX (UCLOCK_FREQ / 25)
/** max retention time for DVB subtitles - unbound */
#define MAX_DELAY_DVBSUB MAX_DELAY_STILL
/** fixed PES header size for teletext (ETSI EN 300 472 4.2) */
#define PES_HEADER_SIZE_TELX 45

/** default minimum duration of audio PES */
#define DEFAULT_AUDIO_PES_MIN_DURATION (UCLOCK_FREQ / 25)
/** max interval between PCRs (ISO/IEC 13818-1 2.7.2) */
#define MAX_PCR_INTERVAL_ISO (UCLOCK_FREQ / 10)
/** max interval between PCRs in DVB conformance */
#define MAX_PCR_INTERVAL_DVB (UCLOCK_FREQ / 25)
/** default interval between PCRs */
#define DEFAULT_PCR_INTERVAL (UCLOCK_FREQ / 15)
/** default interval between SCTE-35 tables */
#define DEFAULT_SCTE35_INTERVAL (UCLOCK_FREQ)
/** default interval between PATs and PMTs in ISO conformance */
#define DEFAULT_PSI_INTERVAL_ISO (UCLOCK_FREQ / 4)
/** max interval between PATs and PMTs, in DVB and ISDB conformance */
#define MAX_PSI_INTERVAL_DVB (UCLOCK_FREQ / 10)
/** max interval between PATs and PMTs, in ATSC conformance */
#define MAX_PSI_INTERVAL_ATSC (UCLOCK_FREQ / 10)
/** max NIT interval */
#define MAX_NIT_INTERVAL (UCLOCK_FREQ * 10)
/** default NIT interval - for 1-packet NIT to comply with TR 101 211 */
#define DEFAULT_NIT_INTERVAL (UCLOCK_FREQ * 10 / 8)
/** max SDT interval */
#define MAX_SDT_INTERVAL (UCLOCK_FREQ * 2)
/** max EIT interval */
#define MAX_EIT_INTERVAL (UCLOCK_FREQ * 2)
/** max TDT interval */
#define MAX_TDT_INTERVAL (UCLOCK_FREQ * 30)
/** default EITs octetrate */
#define DEFAULT_EITS_OCTETRATE 0
/** default AAC encapsulation */
#define DEFAULT_AAC_ENCAPS UREF_MPGA_ENCAPS_ADTS
/** default encoding */
#define DEFAULT_ENCODING "UTF-8"
/** default TSID */
#define DEFAULT_TSID 1
/** default first automatic SID */
#define DEFAULT_SID_AUTO 1
/** default first automatic PID */
#define DEFAULT_PID_AUTO 256

/** define to debug file mode */
#undef DEBUG_FILE

/** @internal @This is the private context of a ts_mux manager. */
struct upipe_ts_mux_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /* inputs */
    /** pointer to ts_encaps manager */
    struct upipe_mgr *ts_encaps_mgr;

    /* PSI */
    /** pointer to ts_psi_join manager */
    struct upipe_mgr *ts_psi_join_mgr;
    /** pointer to ts_psig manager */
    struct upipe_mgr *ts_psig_mgr;
    /** pointer to ts_sig manager */
    struct upipe_mgr *ts_sig_mgr;
    /** pointer to ts_scte35g manager */
    struct upipe_mgr *ts_scte35g_mgr;

    /* ES */
    /** pointer to ts_tstd manager */
    struct upipe_mgr *ts_tstd_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_ts_mux_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_ts_mux_mgr, urefcount, urefcount, urefcount)

/** @hidden */
static int upipe_ts_mux_check(struct upipe *upipe, struct uref *unused);

/** @hidden */
struct upipe_ts_mux_psi_pid;

/** @internal @This is the private context of a ts_mux pipe. */
struct upipe_ts_mux {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;
    /** true if a uclock has been requested */
    bool live;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;

    /** proxy probe */
    struct uprobe probe;
    /** list of input bin requests */
    struct uchain input_request_list;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain output_request_list;

    /** pointer to ts_psig */
    struct upipe *psig;
    /** psi_pid structure for PAT */
    struct upipe_ts_mux_psi_pid *psi_pid_pat;
    /** pointer to ts_psig_program related to NIT */
    struct upipe *psig_nit;

    /** pointer to ts_sig */
    struct upipe *sig;
    /** psi_pid structure for NIT */
    struct upipe_ts_mux_psi_pid *psi_pid_nit;
    /** psi_pid structure for SDT */
    struct upipe_ts_mux_psi_pid *psi_pid_sdt;
    /** psi_pid structure for EIT */
    struct upipe_ts_mux_psi_pid *psi_pid_eit;
    /** psi_pid structure for TDT */
    struct upipe_ts_mux_psi_pid *psi_pid_tdt;

    /** one TS packet of padding */
    struct ubuf *padding;

    /** input flow definition */
    struct uref *flow_def_input;
    /** true if the conformance is guessed from the flow definition */
    bool auto_conformance;
    /** current conformance */
    enum upipe_ts_conformance conformance;
    /** interval between PATs */
    uint64_t pat_interval;
    /** default interval between PMTs */
    uint64_t pmt_interval;
    /** default interval between PCRs */
    uint64_t pcr_interval;
    /** default interval between SCTE-35 tables */
    uint64_t scte35_interval;
    /** interval between NITs */
    uint64_t nit_interval;
    /** interval between SDTs */
    uint64_t sdt_interval;
    /** interval between EITs */
    uint64_t eit_interval;
    /** interval between TDTs */
    uint64_t tdt_interval;
    /** default maximum retention delay */
    uint64_t max_delay;
    /** muxing delay */
    uint64_t mux_delay;
    /** initial cr_prog */
    uint64_t initial_cr_prog;
    /** AAC encapsulation */
    int aac_encaps;
    /** encoding */
    const char *encoding;
    /** last attributed automatic SID */
    uint16_t sid_auto;
    /** last attributed automatic PID */
    uint16_t pid_auto;

    /** octetrate reserved for EITs */
    uint64_t eits_octetrate;
    /** octetrate assigned by the application, or 0 */
    uint64_t fixed_octetrate;
    /** octetrate reserved for padding (and emergency situation) */
    uint64_t padding_octetrate;
    /** total operating octetrate including overheads, PMTs and PAT */
    uint64_t total_octetrate;
    /** calculated required octetrate including overheads, PMTs and PAT */
    uint64_t required_octetrate;
    /** true if @ref upipe_ts_mux_update is running */
    bool octetrate_in_progress;
    /** interval between packets (rounded up, not to be used anywhere
     * critical */
    uint64_t interval;
    /** mux mode */
    enum upipe_ts_mux_mode mode;
    /** MTU */
    size_t mtu;
    /** size of the TB buffer */
    size_t tb_size;

    /** list of PIDs carrying PSI */
    struct uchain psi_pids;
    /** list of PIDs carrying PSI, ordered for splice */
    struct uchain psi_pids_splice;
    /** list of inputs that are actually PSI */
    struct uchain psi_inputs;
    /** max latency of the subpipes */
    uint64_t latency;
    /** date of the current uref (system time, latency taken into account) */
    uint64_t cr_sys;
    /** remainder of the uref_size / octetrate calculation */
    uint64_t cr_sys_remainder;
    /** current aggregation */
    struct uref *uref;
    /** size of current aggregation */
    size_t uref_size;
    /** true during the preroll period */
    bool preroll;

    /** manager of the pseudo inner sink */
    struct upipe_mgr inner_sink_mgr;
    /** pseudo inner sink to get urefs from ts_encaps */
    struct upipe inner_sink;

    /** list of programs */
    struct uchain programs;

    /** manager to create programs */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux, upipe, UPIPE_TS_MUX_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_mux, urefcount, upipe_ts_mux_no_input)
UPIPE_HELPER_VOID(upipe_ts_mux)
UPIPE_HELPER_INNER(upipe_ts_mux, psig)
UPIPE_HELPER_BIN_INPUT(upipe_ts_mux, psig, input_request_list)
UPIPE_HELPER_OUTPUT(upipe_ts_mux, output, flow_def, output_state,
                    output_request_list)
UPIPE_HELPER_UPUMP_MGR(upipe_ts_mux, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_ts_mux, upump, upump_mgr)
UPIPE_HELPER_UREF_MGR(upipe_ts_mux, uref_mgr, uref_mgr_request,
                      upipe_ts_mux_check,
                      upipe_ts_mux_register_output_request,
                      upipe_ts_mux_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_ts_mux, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_mux_check,
                      upipe_ts_mux_register_output_request,
                      upipe_ts_mux_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_ts_mux, uclock, uclock_request,
                    upipe_ts_mux_check,
                    upipe_ts_mux_register_output_request,
                    upipe_ts_mux_unregister_output_request)

UBASE_FROM_TO(upipe_ts_mux, urefcount, urefcount_real, urefcount_real)
UBASE_FROM_TO(upipe_ts_mux, upipe, inner_sink, inner_sink)

/** @hidden */
static void upipe_ts_mux_build_flow_def(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_work(struct upipe *upipe, struct upump **upump_p);
/** @hidden */
static void upipe_ts_mux_update(struct upipe *upipe);
/** @hidden */
static bool upipe_ts_mux_find_sid(struct upipe *upipe, uint16_t sid);
/** @hidden */
static bool upipe_ts_mux_find_pid(struct upipe *upipe, uint16_t pid);
/** @hidden */
static void upipe_ts_mux_free(struct urefcount *urefcount_real);

/** @internal @This is the private context of a program of a ts_mux pipe. */
struct upipe_ts_mux_program {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** SID */
    uint16_t sid;
    /** PMT PID */
    uint16_t pmt_pid;

    /** proxy probe */
    struct uprobe probe;
    /** list of input bin requests */
    struct uchain input_request_list;

    /** pointer to ts_psig_program */
    struct upipe *psig_program;
    /** psi_pid structure for PMT */
    struct upipe_ts_mux_psi_pid *psi_pid_pmt;

    /** pointer to ts_sig_service */
    struct upipe *sig_service;

    /** calculated required octetrate including overheads and PMT */
    uint64_t required_octetrate;

    /** interval between PMTs */
    uint64_t pmt_interval;
    /** interval between PCRs */
    uint64_t pcr_interval;
    /** default interval between SCTE-35 tables */
    uint64_t scte35_interval;
    /** interval between EITs */
    uint64_t eit_interval;
    /** maximum retention delay */
    uint64_t max_delay;
    /** AAC encapsulation */
    int aac_encaps;

    /** input flow definition */
    struct uref *flow_def_input;
    /** list of inputs */
    struct uchain inputs;

    /** manager to create inputs */
    struct upipe_mgr input_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux_program, upipe, UPIPE_TS_MUX_PROGRAM_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_mux_program, urefcount,
                       upipe_ts_mux_program_no_input)
UPIPE_HELPER_VOID(upipe_ts_mux_program)
UPIPE_HELPER_INNER(upipe_ts_mux_program, psig_program)
UPIPE_HELPER_BIN_INPUT(upipe_ts_mux_program, psig_program, input_request_list)

UBASE_FROM_TO(upipe_ts_mux_program, urefcount, urefcount_real, urefcount_real)

UPIPE_HELPER_SUBPIPE(upipe_ts_mux, upipe_ts_mux_program, program,
                     program_mgr, programs, uchain)

/** @hidden */
static void upipe_ts_mux_program_update(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_program_change(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_program_free(struct urefcount *urefcount_real);

/** @internal @This defines the role of an input wrt. PCR management and
 * scheduling. */
enum upipe_ts_mux_input_type {
    /** flow def not received yet */
    UPIPE_TS_MUX_INPUT_UNKNOWN,
    /** video */
    UPIPE_TS_MUX_INPUT_VIDEO,
    /** audio */
    UPIPE_TS_MUX_INPUT_AUDIO,
    /** other (unsuitable for PCR) */
    UPIPE_TS_MUX_INPUT_OTHER,
    /** SCTE-35 PSI sections */
    UPIPE_TS_MUX_INPUT_SCTE35
};

/** @internal @This is the private context of an output of a ts_mux_program
 * subpipe. */
struct upipe_ts_mux_input {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** structure for double-linked lists for PSI inputs */
    struct uchain uchain_psi;

    /** true if the input is in the process of being deleted */
    bool deleted;
    /** input type */
    enum upipe_ts_mux_input_type input_type;
    /** PID */
    uint16_t pid;
    /** octet rate */
    uint64_t octetrate;
    /** buffering duration */
    uint64_t buffer_duration;
    /** true if the output is used for PCR */
    bool pcr;
    /** calculated required octetrate including overheads */
    uint64_t required_octetrate;

    /** proxy probe */
    struct uprobe probe;
    /** list of input bin requests */
    struct uchain input_request_list;
    /** pointer to input */
    struct upipe *input;
    /** pointer to ts_tstd */
    struct upipe *tstd;
    /** pointer to ts_encaps */
    struct upipe *encaps;
    /** pointer to ts_psig_flow */
    struct upipe *psig_flow;
    /** probe passed to ts_encaps */
    struct uprobe encaps_probe;
    /** cr_sys of the next packet */
    uint64_t cr_sys;
    /** dts_sys of the next packet */
    uint64_t dts_sys;
    /** cr_sys of the next PCR */
    uint64_t pcr_sys;
    /** true if the input is ready to output packet */
    bool ready;

    /** psi_pid structure for PSI-based elementary streams */
    struct upipe_ts_mux_psi_pid *psi_pid;
    /** interval between SCTE-35 tables */
    uint64_t scte35_interval;
    /** AAC encapsulation */
    int aac_encaps;

    /** maximum retention delay */
    uint64_t max_delay;
    /** number of access units per second */
    struct urational au_per_sec;
    /** original number of access units per second before aggregation */
    struct urational original_au_per_sec;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux_input, upipe, UPIPE_TS_MUX_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_mux_input, urefcount,
                       upipe_ts_mux_input_no_input)
UPIPE_HELPER_VOID(upipe_ts_mux_input)
UPIPE_HELPER_INNER(upipe_ts_mux_input, input);
UPIPE_HELPER_BIN_INPUT(upipe_ts_mux_input, input, input_request_list)

UBASE_FROM_TO(upipe_ts_mux_input, urefcount, urefcount_real, urefcount_real)
UBASE_FROM_TO(upipe_ts_mux_input, uchain, uchain_psi, uchain_psi)

UPIPE_HELPER_SUBPIPE(upipe_ts_mux_program, upipe_ts_mux_input, input,
                     input_mgr, inputs, uchain)

/** @hidden */
static void upipe_ts_mux_input_free(struct urefcount *urefcount_real);


/*
 * psi_pid structure handling
 */

/** @internal @This is the context of a PID carrying PSI of a ts_mux pipe. */
struct upipe_ts_mux_psi_pid {
    /** external reference count */
    struct urefcount refcount;
    /** internal reference count */
    struct urefcount refcount_real;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** structure for double-linked lists for splice */
    struct uchain uchain_splice;

    /** PID */
    uint16_t pid;
    /** pointer to upipe_ts_mux pipe */
    struct upipe *upipe;

    /** pointer to psi_join inner pipe */
    struct upipe *psi_join;
    /** pointer to encaps inner pipe */
    struct upipe *encaps;

    /** probe passed to psi_join */
    struct uprobe join_probe;
    /** probe passed to encaps */
    struct uprobe encaps_probe;
    /** octetrate of the PID */
    uint64_t octetrate;
    /** cr_sys of the next table */
    uint64_t cr_sys;
    /** dts_sys of the next table */
    uint64_t dts_sys;
};

UBASE_FROM_TO(upipe_ts_mux_psi_pid, uchain, uchain, uchain)
UBASE_FROM_TO(upipe_ts_mux_psi_pid, uchain, uchain_splice, uchain_splice)
UREFCOUNT_HELPER(upipe_ts_mux_psi_pid, refcount, upipe_ts_mux_psi_pid_no_ref)
UREFCOUNT_HELPER(upipe_ts_mux_psi_pid, refcount_real, upipe_ts_mux_psi_pid_free)

/** @internal @This sorts the psi_pids by increasing PID. We do not take into
 * account cr_sys as, in the case of PSIs, packets are prepared for the
 * current muxing date when calling @ref upipe_ts_mux_prepare.
 *
 * @param uchain1 pointer to first psi_pid
 * @param uchain2 pointer to second psi_pid
 * @return -1, 0 or +1
 */
static int upipe_ts_mux_psi_pid_compare(struct uchain *uchain1,
                                        struct uchain *uchain2)
{
    struct upipe_ts_mux_psi_pid *psi_pid1 =
        upipe_ts_mux_psi_pid_from_uchain_splice(uchain1);
    struct upipe_ts_mux_psi_pid *psi_pid2 =
        upipe_ts_mux_psi_pid_from_uchain_splice(uchain2);
    return psi_pid1->pid - psi_pid2->pid;
}

/** @internal @This catches the events from psi_join inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux_psi_pid
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_psi_pid_join_probe(struct uprobe *uprobe,
                                           struct upipe *inner,
                                           int event, va_list args)
{
    struct upipe_ts_mux_psi_pid *psi_pid =
        container_of(uprobe, struct upipe_ts_mux_psi_pid, join_probe);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    if (event != UPROBE_NEW_FLOW_DEF)
        return upipe_throw_proxy(psi_pid->upipe, inner, event, args);

    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t octetrate = 0;
    uref_block_flow_get_octetrate(flow_def, &octetrate);
    uint64_t section_interval = 0;
    uref_ts_flow_get_psi_section_interval(flow_def, &section_interval);
    if (section_interval)
        /* pointer_field + padding overhead */
        octetrate += (uint64_t)(TS_SIZE - TS_HEADER_SIZE) * UCLOCK_FREQ /
                     section_interval;
    /* TS header overhead */
    octetrate += TS_HEADER_SIZE * octetrate / (TS_SIZE - TS_HEADER_SIZE);
    /* round down to nearest TS packet - this is to avoid bouncing up and
     * down around a middle value */
    octetrate -= octetrate % TS_SIZE;
    if (octetrate != psi_pid->octetrate) {
        psi_pid->octetrate = octetrate;
        upipe_ts_mux_update(psi_pid->upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This catches the events from encaps inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux_psi_pid
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_psi_pid_encaps_probe(struct uprobe *uprobe,
                                             struct upipe *inner,
                                             int event, va_list args)
{
    struct upipe_ts_mux_psi_pid *psi_pid =
        container_of(uprobe, struct upipe_ts_mux_psi_pid, encaps_probe);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    if (event != UPROBE_TS_ENCAPS_STATUS)
        return upipe_throw_proxy(psi_pid->upipe, inner, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_TS_ENCAPS_SIGNATURE)

    psi_pid->cr_sys = va_arg(args, uint64_t);
    psi_pid->dts_sys = va_arg(args, uint64_t);
    struct uchain *uchain_splice =
        upipe_ts_mux_psi_pid_to_uchain_splice(psi_pid);
    if (ulist_is_in(uchain_splice))
        ulist_delete(uchain_splice);

    if (psi_pid->cr_sys != UINT64_MAX) {
        struct upipe_ts_mux *upipe_ts_mux =
            upipe_ts_mux_from_upipe(psi_pid->upipe);
        ulist_bubble_reverse(&upipe_ts_mux->psi_pids_splice, uchain_splice,
                             upipe_ts_mux_psi_pid_compare);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This releases a PID from being used for PSI, optionally
 * freeing allocated resources.
 *
 * @param psi_pid psi_pid structure
 */
static void upipe_ts_mux_psi_pid_release(struct upipe_ts_mux_psi_pid *psi_pid)
{
    if (psi_pid)
        upipe_ts_mux_psi_pid_release_refcount(psi_pid);
}

/** @internal @This allocates and initializes a new PID-specific
 * substructure.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @return pointer to allocated substructure
 */
static struct upipe_ts_mux_psi_pid *
    upipe_ts_mux_psi_pid_alloc(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_ts_mux_psi_pid *psi_pid =
        malloc(sizeof(struct upipe_ts_mux_psi_pid));
    if (unlikely(psi_pid == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    struct uref *flow_def = uref_alloc_control(upipe_ts_mux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def,
                         "block.mpegtspsi.")) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, pid)) ||
                 !ubase_check(uref_ts_flow_set_tb_rate(flow_def,
                         TB_RATE_PSI)))) {
        uref_free(flow_def);
        free(psi_pid);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    upipe_ts_mux_psi_pid_init_refcount(psi_pid);
    upipe_ts_mux_psi_pid_init_refcount_real(psi_pid);
    uchain_init(upipe_ts_mux_psi_pid_to_uchain(psi_pid));
    uchain_init(upipe_ts_mux_psi_pid_to_uchain_splice(psi_pid));

    psi_pid->pid = pid;
    /* we do not increase refcount as we will necessarily die before ts_mux */
    psi_pid->upipe = upipe;
    psi_pid->psi_join = psi_pid->encaps = NULL;
    uprobe_init(&psi_pid->join_probe,
                upipe_ts_mux_psi_pid_join_probe, NULL);
    psi_pid->join_probe.refcount =
        upipe_ts_mux_psi_pid_to_refcount_real(psi_pid);
    uprobe_init(&psi_pid->encaps_probe,
                upipe_ts_mux_psi_pid_encaps_probe, NULL);
    psi_pid->encaps_probe.refcount =
        upipe_ts_mux_psi_pid_to_refcount_real(psi_pid);
    psi_pid->cr_sys = psi_pid->dts_sys = UINT64_MAX;
    psi_pid->octetrate = 0;

    /* prepare psi_join and encaps pipes */
    if (unlikely((psi_pid->psi_join =
                  upipe_flow_alloc(ts_mux_mgr->ts_psi_join_mgr,
                      uprobe_pfx_alloc_va(
                          uprobe_use(&psi_pid->join_probe),
                          UPROBE_LOG_VERBOSE,
                          "psi join %"PRIu16, pid),
                      flow_def)) == NULL ||
                 (psi_pid->encaps =
                  upipe_void_alloc_output(psi_pid->psi_join,
                      ts_mux_mgr->ts_encaps_mgr,
                      uprobe_pfx_alloc_va(
                          uprobe_use(&psi_pid->encaps_probe),
                          UPROBE_LOG_VERBOSE,
                          "encaps %"PRIu16, pid))) == NULL)) {
        uref_free(flow_def);
        upipe_ts_mux_psi_pid_release(psi_pid);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    uref_free(flow_def);
    upipe_ts_encaps_set_tb_size(psi_pid->encaps, upipe_ts_mux->tb_size);
    upipe_set_output(psi_pid->encaps,
                     upipe_ts_mux_to_inner_sink(upipe_ts_mux));

    ulist_add(&upipe_ts_mux->psi_pids,
              upipe_ts_mux_psi_pid_to_uchain(psi_pid));
    return psi_pid;
}

/** @internal @This finds a psi_pid by its PID.
 *
 * @param upipe description structure of the pipe
 * @param pid PID
 * @return pointer to substructure
 */
static struct upipe_ts_mux_psi_pid *
    upipe_ts_mux_psi_pid_find(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->psi_pids, uchain) {
        struct upipe_ts_mux_psi_pid *psi_pid =
            upipe_ts_mux_psi_pid_from_uchain(uchain);
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
static struct upipe_ts_mux_psi_pid *
    upipe_ts_mux_psi_pid_use(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_mux_psi_pid *psi_pid =
        upipe_ts_mux_psi_pid_find(upipe, pid);
    if (psi_pid == NULL)
        return upipe_ts_mux_psi_pid_alloc(upipe, pid);

    return upipe_ts_mux_psi_pid_use_refcount(psi_pid);
}

/** @internal @This frees a PSI PID structure.
 *
 * @param psi_pid psi_pid structure
 */
static void upipe_ts_mux_psi_pid_free(struct upipe_ts_mux_psi_pid *psi_pid)
{
    uprobe_clean(&psi_pid->join_probe);
    uprobe_clean(&psi_pid->encaps_probe);
    upipe_ts_mux_psi_pid_clean_refcount_real(psi_pid);
    upipe_ts_mux_psi_pid_clean_refcount(psi_pid);
    free(psi_pid);
}

/** @internal @This is called when there is no more external reference on the
 * PSI PID structure.
 *
 * @param psi_pid psi_pid structure
 */
static void upipe_ts_mux_psi_pid_no_ref(struct upipe_ts_mux_psi_pid *psi_pid)
{
    struct uchain *uchain;
    uchain = upipe_ts_mux_psi_pid_to_uchain(psi_pid);
    if (ulist_is_in(uchain))
        ulist_delete(uchain);
    uchain = upipe_ts_mux_psi_pid_to_uchain_splice(psi_pid);
    if (ulist_is_in(uchain))
        ulist_delete(uchain);
    upipe_release(psi_pid->psi_join);
    upipe_release(psi_pid->encaps);

    upipe_ts_mux_psi_pid_release_refcount_real(psi_pid);
}

/*
 * upipe_ts_mux_input structure handling (derived from upipe structure)
 */

/** @internal @This catches the events from inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux_input
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_input_probe(struct uprobe *uprobe, struct upipe *inner,
                                    int event, va_list args)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        container_of(uprobe, struct upipe_ts_mux_input, probe);
    struct upipe *upipe = upipe_ts_mux_input_to_upipe(upipe_ts_mux_input);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches the events from encaps inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux_input
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_input_encaps_probe(struct uprobe *uprobe,
                                           struct upipe *inner,
                                           int event, va_list args)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        container_of(uprobe, struct upipe_ts_mux_input, encaps_probe);
    struct upipe *upipe = upipe_ts_mux_input_to_upipe(upipe_ts_mux_input);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    if (event != UPROBE_TS_ENCAPS_STATUS)
        return upipe_throw_proxy(upipe, inner, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_TS_ENCAPS_SIGNATURE)

    upipe_ts_mux_input->cr_sys = va_arg(args, uint64_t);
    upipe_ts_mux_input->dts_sys = va_arg(args, uint64_t);
    upipe_ts_mux_input->pcr_sys = va_arg(args, uint64_t);
    upipe_ts_mux_input->ready = !!va_arg(args, int);
    return UBASE_ERR_NONE;
}

/** @internal @This allocates an input subpipe of a ts_mux_program subpipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_mux_input_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(mgr);
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_program_mgr(
                upipe_ts_mux_program_to_upipe(program)->mgr);

    struct upipe *upipe = upipe_ts_mux_input_alloc_void(mgr, uprobe,
                                                        signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    upipe_ts_mux_input_init_urefcount(upipe);
    urefcount_init(upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input),
                   upipe_ts_mux_input_free);
    upipe_ts_mux_input_init_bin_input(upipe);
    uchain_init(upipe_ts_mux_input_to_uchain_psi(upipe_ts_mux_input));
    upipe_ts_mux_input->pcr = false;
    upipe_ts_mux_input->deleted = false;
    upipe_ts_mux_input->input_type = UPIPE_TS_MUX_INPUT_UNKNOWN;
    upipe_ts_mux_input->pid = 0;
    upipe_ts_mux_input->octetrate = 0;
    upipe_ts_mux_input->buffer_duration = 0;
    upipe_ts_mux_input->required_octetrate = 0;
    upipe_ts_mux_input->encaps = NULL;
    upipe_ts_mux_input->psig_flow = NULL;
    upipe_ts_mux_input->cr_sys = UINT64_MAX;
    upipe_ts_mux_input->dts_sys = UINT64_MAX;
    upipe_ts_mux_input->pcr_sys = UINT64_MAX;
    upipe_ts_mux_input->ready = false;
    upipe_ts_mux_input->psi_pid = NULL;
    upipe_ts_mux_input->scte35_interval = program->scte35_interval;
    upipe_ts_mux_input->aac_encaps = program->aac_encaps;
    upipe_ts_mux_input->max_delay = program->max_delay;
    upipe_ts_mux_input->au_per_sec.num = upipe_ts_mux_input->au_per_sec.den = 0;
    upipe_ts_mux_input->original_au_per_sec.num =
        upipe_ts_mux_input->original_au_per_sec.den = 0;

    upipe_ts_mux_input_init_sub(upipe);
    uprobe_init(&upipe_ts_mux_input->probe, upipe_ts_mux_input_probe, NULL);
    upipe_ts_mux_input->probe.refcount =
        upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input);
    uprobe_init(&upipe_ts_mux_input->encaps_probe,
                upipe_ts_mux_input_encaps_probe, NULL);
    upipe_ts_mux_input->encaps_probe.refcount =
        upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input);
    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)->mgr);
    if (unlikely((upipe_ts_mux_input->tstd =
                  upipe_void_alloc(ts_mux_mgr->ts_tstd_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "tstd"))) == NULL ||
                 (upipe_ts_mux_input->encaps =
                  upipe_void_alloc_output(upipe_ts_mux_input->tstd,
                         ts_mux_mgr->ts_encaps_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->encaps_probe),
                             UPROBE_LOG_VERBOSE, "encaps"))) == NULL ||
                 (upipe_ts_mux_input->psig_flow =
                  upipe_void_alloc_output_sub(upipe_ts_mux_input->encaps,
                         program->psig_program,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "psig flow"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    upipe_ts_mux_input_store_bin_input(upipe,
            upipe_use(upipe_ts_mux_input->tstd));
    upipe_ts_encaps_set_tb_size(upipe_ts_mux_input->encaps,
                                upipe_ts_mux->tb_size);
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_mux_input_input(struct upipe *upipe, struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(upipe->mgr);
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_program_mgr(
                upipe_ts_mux_program_to_upipe(program)->mgr);

    upipe_ts_mux_input_bin_input(upipe, uref,
            upipe_ts_mux->live ? NULL : upump_p);

    upipe_ts_mux_work(upipe_ts_mux_to_upipe(upipe_ts_mux), upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_mux_input_set_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_mux_input *input = upipe_ts_mux_input_from_upipe(upipe);
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(upipe->mgr);
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_program_mgr(
                upipe_ts_mux_program_to_upipe(program)->mgr);
    const char *def;
    uint64_t octetrate;

    UBASE_RETURN(uref_ts_flow_set_conformance(
            flow_def,
            upipe_ts_conformance_to_string(upipe_ts_mux->conformance)));

    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (!ubase_ncmp(def, "void.scte35.")) {
        octetrate = 0;

    } else if (ubase_ncmp(def, "block.")) {
        return UBASE_ERR_INVALID;

    } else if (!ubase_check(uref_block_flow_get_octetrate(flow_def,
                                                          &octetrate)) ||
               !octetrate) {
        UBASE_RETURN(uref_block_flow_get_max_octetrate(flow_def, &octetrate));
        if (!octetrate)
            return UBASE_ERR_INVALID;
        upipe_warn_va(upipe, "using max octetrate %"PRIu64" bits/s",
                      octetrate * 8);
    }

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL ||
                 !ubase_check(uref_flow_set_raw_def(flow_def_dup, def)))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_ALLOC;
    }
    UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def_dup, octetrate));

    uint64_t pes_overhead = 0;
    enum upipe_ts_mux_input_type input_type = UPIPE_TS_MUX_INPUT_OTHER;
    uint64_t buffer_size = 0;
    uint64_t max_delay = MAX_DELAY;
    struct urational au_per_sec, original_au_per_sec;
    au_per_sec.num = au_per_sec.den = 0;
    original_au_per_sec.num = original_au_per_sec.den = 0;
    bool au_irregular = true;
    bool pes_alignment = false;
    bool has_random = false;
    uint64_t coalesce_latency = 0;

    if (strstr(def, ".pic.sub.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_OTHER;
        if (!ubase_ncmp(def, "block.dvb_teletext.")) {
            buffer_size = BS_TELX;
            max_delay = MAX_DELAY_TELX;
            au_irregular = false;
            pes_alignment = true;
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_header(flow_def_dup,
                                                PES_HEADER_SIZE_TELX));
            UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup,
                                                TB_RATE_TELX));

            /* PES header overhead - worst case is a 30 Hz system */
            au_per_sec.num = 30;
            au_per_sec.den = 1;
            uref_pic_flow_get_fps(flow_def, &au_per_sec);
            /* PES header overhead */
            pes_overhead += PES_HEADER_SIZE_TELX *
                (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;

        } else if (!ubase_ncmp(def, "block.dvb_subtitle.")) {
            au_irregular = false;
            pes_alignment = true;
            buffer_size = BS_DVBSUB;
            max_delay = MAX_DELAY_DVBSUB;
            uref_block_flow_get_buffer_size(flow_def, &buffer_size);
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));
            UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup,
                                                octetrate));

            /* PES header overhead - worst case one subtitle by frame in a
             * 30 Hz system */
            au_per_sec.num = 30;
            au_per_sec.den = 1;
            pes_overhead += PES_HEADER_SIZE_PTS *
                (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
        }

    } else if (strstr(def, ".pic.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_VIDEO;
        has_random = true;
        if (!ubase_ncmp(def, "block.mpeg1video.")) {
            pes_alignment = true;
        } else if (!ubase_ncmp(def, "block.mpeg2video.")) {
            pes_alignment = true;
        } else if (!ubase_ncmp(def, "block.mpeg4.")) {
            max_delay = MAX_DELAY_14496;
        } else if (!ubase_ncmp(def, "block.h264.")) {
            max_delay = MAX_DELAY_14496;
        } else if (!ubase_ncmp(def, "block.hevc.")) {
            max_delay = MAX_DELAY_HEVC;
        }
        UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_VIDEO_MPEG));

        uint64_t max_octetrate = octetrate;
        uref_block_flow_get_max_octetrate(flow_def, &max_octetrate);
        /* ISO/IEC 13818-1 2.4.2.3 */
        UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup,
                                                    max_octetrate * 6 / 5));

        if (!ubase_check(uref_block_flow_get_buffer_size(flow_def,
                                                         &buffer_size) &&
            !ubase_check(uref_block_flow_get_max_buffer_size(flow_def,
                                                         &buffer_size)))) {
            uref_free(flow_def_dup);
            return UBASE_ERR_INVALID;
        }

        /* PES header overhead - worst case 60 Hz system */
        au_per_sec.num = 60;
        au_per_sec.den = 1;
        uref_pic_flow_get_fps(flow_def, &au_per_sec);
        /* PES header overhead */
        pes_overhead += PES_HEADER_SIZE_PTSDTS *
            (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;

    } else if (strstr(def, ".sound.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_AUDIO;
        uint64_t pes_min_duration = DEFAULT_AUDIO_PES_MIN_DURATION;
        buffer_size = BS_ADTS_2;

        if (!ubase_ncmp(def, "block.mp2.") || !ubase_ncmp(def, "block.mp3.")) {
            pes_alignment = true;
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                 PES_STREAM_ID_AUDIO_MPEG));
        } else if (!ubase_ncmp(def, "block.aac.") ||
                   !ubase_ncmp(def, "block.aac_latm.")) {
            uint8_t channels = 2;
            uref_sound_flow_get_channels(flow_def_dup, &channels);
            if (channels > 12)
                buffer_size = BS_ADTS_48;
            else if (channels > 8)
                buffer_size = BS_ADTS_12;
            else if (channels > 2)
                buffer_size = BS_ADTS_8;
            /* SCTE 193-2 6.2.1 and 6.3.1 */
            if (upipe_ts_mux->conformance == UPIPE_TS_CONFORMANCE_ATSC ||
                upipe_ts_mux->conformance == UPIPE_TS_CONFORMANCE_ISDB)
                pes_alignment = true;

            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_AUDIO_MPEG));
        } else if (!ubase_ncmp(def, "block.ac3.")) {
            buffer_size = BS_A52;
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                 PES_STREAM_ID_PRIVATE_1));
            pes_min_duration = 0;
        } else if (!ubase_ncmp(def, "block.eac3.")) {
            buffer_size = BS_A52;
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));
            pes_min_duration = 0;
        } else if (!ubase_ncmp(def, "block.dts.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));
            pes_min_duration = 0;
        } else if (!ubase_ncmp(def, "block.opus.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                 PES_STREAM_ID_PRIVATE_1));
        }

        UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup, TB_RATE_AUDIO));
        if (!ubase_check(uref_ts_flow_get_pes_min_duration(flow_def_dup,
                                                           &pes_min_duration)))
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_min_duration(flow_def_dup,
                                                           pes_min_duration));

        uint64_t rate = 48000, samples = 1152;
        uref_sound_flow_get_rate(flow_def, &rate);
        uref_sound_flow_get_samples(flow_def, &samples);
        original_au_per_sec.num = rate;
        original_au_per_sec.den = samples;
        urational_simplify(&original_au_per_sec);

        unsigned int nb_frames = 1;
        while (samples * nb_frames * UCLOCK_FREQ / rate < pes_min_duration)
            nb_frames++;
        samples *= nb_frames;
        coalesce_latency = samples * nb_frames * UCLOCK_FREQ / rate;

        au_per_sec.num = rate;
        au_per_sec.den = samples;
        urational_simplify(&au_per_sec);

        /* PES header overhead */
        pes_overhead += PES_HEADER_SIZE_PTS *
            (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
    }

    input->au_per_sec = au_per_sec;
    if (!original_au_per_sec.den)
        original_au_per_sec = au_per_sec;
    input->original_au_per_sec = original_au_per_sec;

    uint64_t ts_overhead = TS_HEADER_SIZE *
        (octetrate + pes_overhead + TS_SIZE - TS_HEADER_SIZE - 1) /
        (TS_SIZE - TS_HEADER_SIZE);

    uint64_t pid = input->pid;
    uref_ts_flow_get_pid(flow_def, &pid);
    if (pid == 0) {
        do {
            pid = upipe_ts_mux->pid_auto++;
        } while (upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(upipe_ts_mux),
                                       pid));
        if (pid >= MAX_PIDS) {
            uref_free(flow_def_dup);
            return UBASE_ERR_BUSY;
        }
    }
    UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def_dup, pid));

    if (!ubase_ncmp(def, "void.scte35.")) {
        input_type = UPIPE_TS_MUX_INPUT_SCTE35;
        input->cr_sys = UINT64_MAX;
        input->dts_sys = UINT64_MAX;
        input->pcr_sys = UINT64_MAX;
        input->ready = false;
        if (!ulist_is_in(upipe_ts_mux_input_to_uchain_psi(input)))
            ulist_add(&upipe_ts_mux->psi_inputs,
                      upipe_ts_mux_input_to_uchain_psi(input));

        struct upipe_ts_mux_mgr *ts_mux_mgr =
            upipe_ts_mux_mgr_from_upipe_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)->mgr);
        struct upipe *scte35g;
        if (unlikely((scte35g =
                      upipe_void_alloc(ts_mux_mgr->ts_scte35g_mgr,
                             uprobe_pfx_alloc_va(
                                 uprobe_use(&input->probe),
                                 UPROBE_LOG_VERBOSE, "scte35g"))) == NULL) ||
                     !ubase_check(upipe_set_flow_def(scte35g, flow_def))) {
            upipe_release(scte35g);
            return UBASE_ERR_ALLOC;
        }
        upipe_ts_mux_set_scte35_interval(scte35g, input->scte35_interval);
        upipe_ts_mux_input_store_bin_input(upipe, scte35g);

        if (input->psi_pid != NULL && input->psi_pid->pid != pid) {
            upipe_ts_mux_psi_pid_release(input->psi_pid);
            input->psi_pid = NULL;
        }
        if (input->psi_pid == NULL)
            input->psi_pid =
                upipe_ts_mux_psi_pid_use(upipe_ts_mux_to_upipe(upipe_ts_mux),
                                         pid);

        if (unlikely(!ubase_check(upipe_void_spawn_output_sub(scte35g,
                             input->psi_pid->psi_join,
                             uprobe_pfx_alloc(
                                 uprobe_use(&input->probe),
                                 UPROBE_LOG_VERBOSE, "scte35 psi_join")))))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

        UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup, TB_RATE_PSI));

        if (!ubase_check(upipe_set_flow_def(input->psig_flow, flow_def_dup))) {
            uref_free(flow_def_dup);
            return UBASE_ERR_ALLOC;
        }

    } else { /* standard PES */
        upipe_ts_mux_input_store_bin_input(upipe, upipe_use(input->tstd));
        upipe_ts_mux_set_max_delay(input->tstd, input->max_delay);

        if (input->psi_pid != NULL) {
            upipe_ts_mux_psi_pid_release(input->psi_pid);
            input->psi_pid = NULL;
        }

        if (au_irregular && pes_alignment) {
            /* TS padding overhead */
            pes_overhead += (TS_SIZE - TS_HEADER_SIZE) *
                (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
        }

        if (has_random) {
            /* adaptation field added for each random access point, worst
             * case for each frame */
            pes_overhead += (TS_HEADER_SIZE_AF - TS_HEADER_SIZE) *
                (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
        }

        if (buffer_size > max_delay * octetrate / UCLOCK_FREQ) {
            buffer_size = max_delay * octetrate / UCLOCK_FREQ;
            upipe_warn_va(upipe, "lowering buffer size to %"PRIu64" octets to comply with T-STD max retention", buffer_size);
        }
        if (buffer_size > input->max_delay * octetrate / UCLOCK_FREQ) {
            buffer_size = input->max_delay * octetrate / UCLOCK_FREQ;
            upipe_warn_va(upipe, "lowering buffer size to %"PRIu64" octets to comply with configured max retention", buffer_size);
        }
        UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def_dup,
                                                           buffer_size));
        input->buffer_duration =
            UCLOCK_FREQ * buffer_size / octetrate + coalesce_latency;

        if (pes_alignment) {
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_alignment(flow_def_dup))
        }
    }

    if (!ubase_check(upipe_set_flow_def(input->input, flow_def_dup))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_ALLOC;
    }
    uref_free(flow_def_dup);

    input->input_type = input_type;
    input->pid = pid;
    input->octetrate = octetrate;
    input->required_octetrate = octetrate + pes_overhead + ts_overhead;
    input->pcr = false; /* reset PCR state to trigger a new PMT */

    uint64_t latency = 0;
    uref_clock_get_latency(flow_def, &latency);
    /* we never lower latency */
    if (latency + input->buffer_duration > upipe_ts_mux->latency) {
        upipe_ts_mux->latency = latency + input->buffer_duration;
        upipe_ts_mux_build_flow_def(upipe_ts_mux_to_upipe(upipe_ts_mux));
    }
    if (upipe_ts_mux->live && au_per_sec.den) { /* live mode */
        upipe_set_max_length(input->encaps,
                (MIN_BUFFERING + upipe_ts_mux->latency) * au_per_sec.num /
                au_per_sec.den / UCLOCK_FREQ);
    } else /* file mode */
        upipe_set_max_length(input->encaps, UINT_MAX);

    upipe_notice_va(upipe,
            "adding %s on PID %"PRIu64" (%"PRIu64" bits/s), latency %"PRIu64" ms, buffer %"PRIu64" ms",
            def, pid, octetrate * 8, latency * 1000 / UCLOCK_FREQ,
            input->buffer_duration * 1000 / UCLOCK_FREQ);
    upipe_ts_mux_program_change(upipe_ts_mux_program_to_upipe(program));
    upipe_ts_mux_program_update(upipe_ts_mux_program_to_upipe(program));
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_ts_mux_input_provide_flow_format(struct upipe *upipe,
                                                  struct urequest *request)
{
    struct upipe_ts_mux_input *input = upipe_ts_mux_input_from_upipe(upipe);
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    /* we never want global headers */
    uref_flow_delete_global(flow_format);
    const char *def;
    if (likely(ubase_check(uref_flow_get_def(flow_format, &def)))) {
        if (!ubase_ncmp(def, "block.h264.") || !ubase_ncmp(def, "block.hevc."))
            uref_h26x_flow_set_encaps(flow_format, UREF_H26X_ENCAPS_ANNEXB);
        else if (!ubase_ncmp(def, "block.aac.") ||
                 !ubase_ncmp(def, "block.aac_latm."))
            uref_mpga_flow_set_encaps(flow_format, input->aac_encaps);
    }
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This returns the current SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_mux_input_get_scte35_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_input->scte35_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_mux_input_set_scte35_interval(struct upipe *upipe,
                                                  uint64_t interval)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    upipe_ts_mux_input->scte35_interval = interval;
    if (upipe_ts_mux_input->input_type == UPIPE_TS_MUX_INPUT_SCTE35)
        return upipe_ts_mux_set_scte35_interval(upipe_ts_mux_input->input,
                                                interval);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current encapsulation for AAC streams.
 *
 * @param upipe description structure of the pipe
 * @param encaps_p filled in with the encapsulation
 * @return an error code
 */
static int _upipe_ts_mux_input_get_aac_encaps(struct upipe *upipe,
                                              int *encaps_p)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    assert(encaps_p != NULL);
    *encaps_p = upipe_ts_mux_input->aac_encaps;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the encapsulation for AAC streams.
 *
 * @param upipe description structure of the pipe
 * @param encaps encapsulation
 * @return an error code
 */
static int _upipe_ts_mux_input_set_aac_encaps(struct upipe *upipe, int encaps)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    upipe_ts_mux_input->aac_encaps = encaps;

    /* Should we restart negotiation? */
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts_mux_input
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mux_input_control(struct upipe *upipe,
                                      int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_mux_input_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_ts_mux_input_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_input_set_flow_def(upipe, flow_def);
        }
        case UPIPE_TS_MUX_GET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_input_get_scte35_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_input_set_scte35_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_AAC_ENCAPS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            int *encaps_p = va_arg(args, int *);
            return _upipe_ts_mux_input_get_aac_encaps(upipe, encaps_p);
        }
        case UPIPE_TS_MUX_SET_AAC_ENCAPS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            int encaps = va_arg(args, int);
            return _upipe_ts_mux_input_set_aac_encaps(upipe, encaps);
        }

        case UPIPE_GET_MAX_LENGTH:
        case UPIPE_SET_MAX_LENGTH:
        case UPIPE_TS_MUX_GET_CC:
        case UPIPE_TS_MUX_SET_CC: {
            struct upipe_ts_mux_input *upipe_ts_mux_input =
                upipe_ts_mux_input_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux_input->encaps, command, args);
        }
        case UPIPE_FLUSH: {
            struct upipe_ts_mux_input *upipe_ts_mux_input =
                upipe_ts_mux_input_from_upipe(upipe);
            UBASE_RETURN(upipe_control_va(upipe_ts_mux_input->encaps,
                                          command, args))

            struct upipe_ts_mux_program *program =
                upipe_ts_mux_program_from_input_mgr(upipe->mgr);
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_program_mgr(
                        upipe_ts_mux_program_to_upipe(program)->mgr);
            upipe_ts_mux_work(upipe_ts_mux_to_upipe(upipe_ts_mux), NULL);
            return UBASE_ERR_NONE;
        }
        case UPIPE_BIN_GET_LAST_INNER: {
            struct upipe_ts_mux_input *upipe_ts_mux_input =
                upipe_ts_mux_input_from_upipe(upipe);
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_ts_mux_input->psig_flow;
            return (*p != NULL) ? UBASE_ERR_NONE : UBASE_ERR_UNHANDLED;
        }

        case UPIPE_TS_MUX_GET_MAX_DELAY:
        case UPIPE_TS_MUX_SET_MAX_DELAY: {
            struct upipe_ts_mux_input *upipe_ts_mux_input =
                upipe_ts_mux_input_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux_input->input, command, args);
        }
        default:
            break;
    }

    return upipe_ts_mux_input_control_bin_input(upipe, command, args);
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_mux_input_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_mux_input_to_upipe(upipe_ts_mux_input);
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(upipe->mgr);

    upipe_ts_mux_input_clean_sub(upipe);
    if (!upipe_single(upipe_ts_mux_program_to_upipe(program)))
        upipe_ts_mux_program_change(upipe_ts_mux_program_to_upipe(program));

    upipe_throw_dead(upipe);

    uprobe_clean(&upipe_ts_mux_input->probe);
    uprobe_clean(&upipe_ts_mux_input->encaps_probe);
    urefcount_clean(urefcount_real);
    upipe_ts_mux_input_clean_urefcount(upipe);
    upipe_ts_mux_input_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_input_no_input(struct upipe *upipe)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(upipe->mgr);
    struct upipe_ts_mux *mux = upipe_ts_mux_from_program_mgr(
                upipe_ts_mux_program_to_upipe(program)->mgr);
    upipe_use(upipe_ts_mux_to_upipe(mux));

    upipe_ts_mux_input->deleted = true;
    if (upipe_ts_mux_input->input_type == UPIPE_TS_MUX_INPUT_SCTE35) {
        ulist_delete(upipe_ts_mux_input_to_uchain_psi(upipe_ts_mux_input));
        upipe_release(upipe_ts_mux_input->encaps);
        upipe_ts_mux_input->encaps = NULL;
    } else {
        upipe_ts_encaps_eos(upipe_ts_mux_input->encaps);
        if (!upipe_ts_mux_input->ready) {
            upipe_release(upipe_ts_mux_input->encaps);
            upipe_ts_mux_input->encaps = NULL;
        }
    }
    upipe_release(upipe_ts_mux_input->tstd);
    upipe_ts_mux_input->tstd = NULL;
    upipe_release(upipe_ts_mux_input->psig_flow);
    upipe_ts_mux_input->psig_flow = NULL;
    upipe_ts_mux_input_clean_bin_input(upipe);
    upipe_ts_mux_psi_pid_release(upipe_ts_mux_input->psi_pid);
    upipe_ts_mux_input->psi_pid = NULL;
    urefcount_release(upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input));

    upipe_ts_mux_work(upipe_ts_mux_to_upipe(mux), NULL);
    upipe_release(upipe_ts_mux_to_upipe(mux));
}

/** @internal @This initializes the output manager for a ts_mux_program
 * subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_init_input_mgr(struct upipe *upipe)
{
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_upipe(upipe);
    struct upipe_mgr *input_mgr = &program->input_mgr;
    input_mgr->refcount = upipe_ts_mux_program_to_urefcount(program);
    input_mgr->signature = UPIPE_TS_MUX_INPUT_SIGNATURE;
    input_mgr->upipe_alloc = upipe_ts_mux_input_alloc;
    input_mgr->upipe_input = upipe_ts_mux_input_input;
    input_mgr->upipe_control = upipe_ts_mux_input_control;
    input_mgr->upipe_mgr_control = NULL;
}


/*
 * upipe_ts_mux_program structure handling (derived from upipe structure)
 */

/** @internal @This updates the SI generator.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_update_sig(struct upipe *upipe)
{
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_upipe(upipe);
    struct upipe_ts_mux *mux =
        upipe_ts_mux_from_program_mgr(upipe->mgr);

    if (mux->sig == NULL) {
        upipe_release(program->sig_service);
        program->sig_service = NULL;
        return;
    }

    if (program->sig_service != NULL)
        return;

    if (unlikely((program->sig_service =
                  upipe_void_alloc_sub(mux->sig,
                         uprobe_pfx_alloc(uprobe_use(&program->probe),
                                          UPROBE_LOG_VERBOSE,
                                          "sig service"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
}

/** @internal @This catches the events from inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux_program
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static int upipe_ts_mux_program_probe(struct uprobe *uprobe,
                                      struct upipe *inner,
                                      int event, va_list args)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        container_of(uprobe, struct upipe_ts_mux_program, probe);
    struct upipe *upipe = upipe_ts_mux_program_to_upipe(upipe_ts_mux_program);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a program subpipe of a ts_mux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_mux_program_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature,
                                                va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_program_mgr(mgr);
    if (unlikely(upipe_ts_mux->uref_mgr == NULL))
        return NULL;

    struct upipe *upipe = upipe_ts_mux_program_alloc_void(mgr, uprobe,
                                                          signature,
                                                          args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program_init_urefcount(upipe);
    urefcount_init(upipe_ts_mux_program_to_urefcount_real(upipe_ts_mux_program),
                   upipe_ts_mux_program_free);
    upipe_ts_mux_program_init_bin_input(upipe);
    upipe_ts_mux_program_init_input_mgr(upipe);
    upipe_ts_mux_program_init_sub_inputs(upipe);
    upipe_ts_mux_program->flow_def_input = NULL;
    upipe_ts_mux_program->psi_pid_pmt = NULL;
    upipe_ts_mux_program->sig_service = NULL;
    upipe_ts_mux_program->sid = 0;
    upipe_ts_mux_program->pmt_pid = 8192;
    upipe_ts_mux_program->pmt_interval = upipe_ts_mux->pmt_interval;
    upipe_ts_mux_program->eit_interval = upipe_ts_mux->eit_interval;
    upipe_ts_mux_program->pcr_interval = upipe_ts_mux->pcr_interval;
    upipe_ts_mux_program->scte35_interval = upipe_ts_mux->scte35_interval;
    upipe_ts_mux_program->aac_encaps = upipe_ts_mux->aac_encaps;
    upipe_ts_mux_program->max_delay = upipe_ts_mux->max_delay;
    upipe_ts_mux_program->required_octetrate = 0;
    upipe_ts_mux_program_init_sub(upipe);

    uprobe_init(&upipe_ts_mux_program->probe, upipe_ts_mux_program_probe, NULL);
    upipe_ts_mux_program->probe.refcount =
        upipe_ts_mux_program_to_urefcount_real(upipe_ts_mux_program);

    upipe_throw_ready(upipe);

    struct upipe *psig_program;
    if (unlikely((psig_program =
                  upipe_void_alloc_sub(upipe_ts_mux->psig,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_mux_program->probe),
                             UPROBE_LOG_VERBOSE, "psig program"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }
    upipe_ts_mux_program_store_bin_input(upipe, psig_program);

    upipe_ts_mux_program_update_sig(upipe);
    upipe_ts_mux_program_update(upipe);
    return upipe;
}

/** @This calculates the total octetrate used by a program and updates the
 * mux.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_update(struct upipe *upipe)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    uint64_t required_octetrate = 0;

    if (upipe_ts_mux_program->pcr_interval)
        required_octetrate += (uint64_t)TS_SIZE *
             ((UCLOCK_FREQ + upipe_ts_mux_program->pcr_interval - 1) /
              upipe_ts_mux_program->pcr_interval);

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        required_octetrate += input->required_octetrate;
    }

    upipe_ts_mux_program->required_octetrate = required_octetrate;

    struct upipe_ts_mux *upipe_ts_mux =
        upipe_ts_mux_from_program_mgr(upipe->mgr);
    upipe_ts_mux_update(upipe_ts_mux_to_upipe(upipe_ts_mux));
}

/** @This is called when the program definition is changed (input added or
 * deleted).
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_change(struct upipe *upipe)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    uint64_t pcr_pid = UNDEF_PCR, old_pcr_pid = UNDEF_PCR;
    struct upipe_ts_mux_input *pcr_input = NULL, *old_pcr_input = NULL;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        if (input->pcr) {
            old_pcr_pid = input->pid;
            old_pcr_input = input;
        }
        if (input->input_type == UPIPE_TS_MUX_INPUT_OTHER ||
            input->input_type == UPIPE_TS_MUX_INPUT_SCTE35 ||
            input->input_type == UPIPE_TS_MUX_INPUT_UNKNOWN)
            continue;
        if (pcr_input == NULL ||
            (pcr_input->input_type == UPIPE_TS_MUX_INPUT_AUDIO &&
             input->input_type == UPIPE_TS_MUX_INPUT_VIDEO)) {
            pcr_pid = input->pid;
            pcr_input = input;
        }
    }

    if (pcr_pid != old_pcr_pid) {
        if (old_pcr_pid != UNDEF_PCR) {
            old_pcr_input->pcr = false;
            if (old_pcr_input->encaps != NULL)
                upipe_ts_mux_set_pcr_interval(old_pcr_input->encaps, 0);
        }

        if (pcr_pid != UNDEF_PCR) {
            pcr_input->pcr = true;
            if (pcr_input->encaps != NULL)
                upipe_ts_mux_set_pcr_interval(pcr_input->encaps,
                        upipe_ts_mux_program->pcr_interval);
        }
        upipe_ts_psig_program_set_pcr_pid(upipe_ts_mux_program->psig_program,
                                          pcr_pid);
    }
}

/** @internal @This sets the initial cr_prog of the program to the given value.
 *
 * @param upipe description structure of the pipe
 * @param cr_prog initial cr_prog value
 */
static void upipe_ts_mux_program_init_cr_prog(struct upipe *upipe,
                                              uint64_t cr_prog)
{
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_upipe(upipe);
    uint64_t min_cr_sys = UINT64_MAX;

    struct uchain *uchain;
    ulist_foreach (&program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        if (input->cr_sys < min_cr_sys)
            min_cr_sys = input->cr_sys;
    }

    ulist_foreach (&program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        upipe_ts_mux_set_cr_prog(input->encaps,
                                 cr_prog + input->cr_sys - min_cr_sys);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_mux_program_set_flow_def(struct upipe *upipe,
                                             struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_upipe(upipe);
    struct upipe_ts_mux *mux =
        upipe_ts_mux_from_program_mgr(upipe->mgr);
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    uint64_t sid = 0;
    uref_flow_get_id(flow_def, &sid);
    if (sid == 0) {
        do {
            sid = mux->sid_auto++;
        } while (upipe_ts_mux_find_sid(upipe_ts_mux_to_upipe(mux),
                                       sid));
        if (sid >= MAX_SIDS) {
            uref_free(flow_def_dup);
            return UBASE_ERR_BUSY;
        } else
            UBASE_FATAL(upipe, uref_flow_set_id(flow_def_dup, sid));
    }

    uint64_t pid = 0;
    uref_ts_flow_get_pid(flow_def, &pid);
    if (pid == 0) {
        do {
            pid = mux->pid_auto++;
        } while (upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(mux),
                                       pid));
        if (pid >= MAX_PIDS) {
            uref_free(flow_def_dup);
            return UBASE_ERR_BUSY;
        } else
            UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def_dup, pid));
    }

    if (program->pmt_pid != pid) {
        upipe_ts_mux_psi_pid_release(program->psi_pid_pmt);
        program->psi_pid_pmt =
            upipe_ts_mux_psi_pid_use(upipe_ts_mux_to_upipe(mux), pid);
        struct upipe *psi_join;
        if (unlikely((psi_join =
                      upipe_void_alloc_output_sub(program->psig_program,
                             program->psi_pid_pmt->psi_join,
                             uprobe_pfx_alloc(
                                 uprobe_use(&program->probe),
                                 UPROBE_LOG_VERBOSE,
                                 "pmt psi_join"))) == NULL))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        upipe_release(psi_join);
    }

    uref_free(program->flow_def_input);
    program->flow_def_input = flow_def_dup;

    if (!ubase_check(upipe_set_flow_def(program->psig_program, flow_def_dup))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_INVALID;
    }
    if (program->sig_service != NULL)
        upipe_set_flow_def(program->sig_service, flow_def_dup);

    bool changed = program->sid != sid || program->pmt_pid != pid;
    program->sid = sid;
    program->pmt_pid = pid;

    if (changed)
        upipe_notice_va(upipe, "adding program %"PRIu64" on PID %"PRIu64,
                        sid, pid);
    else
        upipe_notice_va(upipe, "changing program %"PRIu64" on PID %"PRIu64,
                        sid, pid);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_mux_program_get_pmt_interval(struct upipe *upipe,
                                                 uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->pmt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_mux_program_set_pmt_interval(struct upipe *upipe,
                                                 uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->pmt_interval = interval;
    upipe_ts_mux_program_update(upipe); /* will trigger set_pmt_interval */
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current EIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_mux_program_get_eit_interval(struct upipe *upipe,
                                                 uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->eit_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the EIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_mux_program_set_eit_interval(struct upipe *upipe,
                                                 uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->eit_interval = interval;
    upipe_ts_mux_program_update(upipe); /* will trigger set_eit_interval */
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_mux_program_get_pcr_interval(struct upipe *upipe,
                                                 uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->pcr_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_mux_program_set_pcr_interval(struct upipe *upipe,
                                                 uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->pcr_interval = interval;
    upipe_ts_mux_program_update(upipe); /* will trigger set_pcr_interval */
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_mux_program_get_scte35_interval(struct upipe *upipe,
                                                    uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->scte35_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_mux_program_set_scte35_interval(struct upipe *upipe,
                                                    uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->scte35_interval = interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        upipe_ts_mux_set_scte35_interval(upipe_ts_mux_input_to_upipe(input),
                                         interval);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled in with the delay
 * @return an error code
 */
static int upipe_ts_mux_program_get_max_delay(struct upipe *upipe,
                                              uint64_t *delay_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(delay_p != NULL);
    *delay_p = upipe_ts_mux_program->max_delay;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return an error code
 */
static int upipe_ts_mux_program_set_max_delay(struct upipe *upipe,
                                              uint64_t delay)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->max_delay = delay;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        upipe_ts_mux_set_max_delay(upipe_ts_mux_input_to_upipe(input), delay);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current encapsulation for AAC streams.
 *
 * @param upipe description structure of the pipe
 * @param encaps_p filled in with the encapsulation
 * @return an error code
 */
static int _upipe_ts_mux_program_get_aac_encaps(struct upipe *upipe,
                                                int *encaps_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(encaps_p != NULL);
    *encaps_p = upipe_ts_mux_program->aac_encaps;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the encapsulation for AAC streams.
 *
 * @param upipe description structure of the pipe
 * @param encaps encapsulation
 * @return an error code
 */
static int _upipe_ts_mux_program_set_aac_encaps(struct upipe *upipe, int encaps)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->aac_encaps = encaps;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        upipe_ts_mux_set_aac_encaps(upipe_ts_mux_input_to_upipe(input), encaps);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts_mux_program pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mux_program_control(struct upipe *upipe,
                                        int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_mux_program_control_inputs(upipe, command, args));
    UBASE_HANDLED_RETURN(
        upipe_ts_mux_program_control_super(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_program_set_flow_def(upipe, flow_def);
        }

        case UPIPE_TS_MUX_GET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_pmt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_pmt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_EIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_eit_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_EIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_eit_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PCR_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_pcr_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PCR_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_pcr_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_scte35_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_scte35_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_MAX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *delay_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_max_delay(upipe, delay_p);
        }
        case UPIPE_TS_MUX_SET_MAX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t delay = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_max_delay(upipe, delay);
        }
        case UPIPE_TS_MUX_GET_AAC_ENCAPS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            int *encaps_p = va_arg(args, int *);
            return _upipe_ts_mux_program_get_aac_encaps(upipe, encaps_p);
        }
        case UPIPE_TS_MUX_SET_AAC_ENCAPS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            int encaps = va_arg(args, int);
            return _upipe_ts_mux_program_set_aac_encaps(upipe, encaps);
        }

        case UPIPE_TS_MUX_GET_VERSION:
        case UPIPE_TS_MUX_SET_VERSION:
        case UPIPE_TS_MUX_FREEZE_PSI: {
            struct upipe_ts_mux_program *upipe_ts_mux_program =
                upipe_ts_mux_program_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux_program->psig_program,
                                    command, args);
        }

        case UPIPE_TS_MUX_GET_CC:
        case UPIPE_TS_MUX_SET_CC: {
            struct upipe_ts_mux_program *upipe_ts_mux_program =
                upipe_ts_mux_program_from_upipe(upipe);
            if (upipe_ts_mux_program->psi_pid_pmt == NULL)
                return UBASE_ERR_UNHANDLED;
            return upipe_control_va(upipe_ts_mux_program->psi_pid_pmt->encaps,
                                    command, args);
        }

        default:
            break;
    }

    return upipe_ts_mux_program_control_bin_input(upipe, command, args);
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_mux_program_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_mux_program_to_upipe(upipe_ts_mux_program);

    upipe_throw_dead(upipe);

    uprobe_clean(&upipe_ts_mux_program->probe);
    urefcount_clean(urefcount_real);
    upipe_ts_mux_program_clean_urefcount(upipe);
    upipe_ts_mux_program_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_no_input(struct upipe *upipe)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);

    uref_free(upipe_ts_mux_program->flow_def_input);
    upipe_ts_mux_program_clean_bin_input(upipe);
    upipe_ts_mux_psi_pid_release(upipe_ts_mux_program->psi_pid_pmt);
    upipe_release(upipe_ts_mux_program->sig_service);
    upipe_ts_mux_program_clean_sub_inputs(upipe);
    upipe_ts_mux_program_clean_sub(upipe);

    urefcount_release(upipe_ts_mux_program_to_urefcount_real(upipe_ts_mux_program));
}

/** @internal @This initializes the program manager for a ts_mux pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_mux->program_mgr;
    program_mgr->refcount = upipe_ts_mux_to_urefcount(upipe_ts_mux);
    program_mgr->signature = UPIPE_TS_MUX_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_mux_program_alloc;
    program_mgr->upipe_input = NULL;
    program_mgr->upipe_control = upipe_ts_mux_program_control;
    program_mgr->upipe_mgr_control = NULL;
}


/*
 * upipe_ts_mux inner sink (to receive urefs from ts_encaps)
 */

/** @internal @This processes control commands.
 *
 * @param sink description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mux_inner_sink_control(struct upipe *sink,
                                           int command, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_inner_sink(sink);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);
    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_ts_mux_control_output(upipe, command, args);
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This initializes the inner pseudo sink pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_init_inner_sink(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct upipe_mgr *inner_sink_mgr = &upipe_ts_mux->inner_sink_mgr;
    inner_sink_mgr->refcount = NULL;
    inner_sink_mgr->signature = UPIPE_TS_MUX_INNER_SINK_SIGNATURE;
    inner_sink_mgr->upipe_alloc = NULL;
    inner_sink_mgr->upipe_input = NULL;
    inner_sink_mgr->upipe_control = upipe_ts_mux_inner_sink_control;
    inner_sink_mgr->upipe_mgr_control = NULL;

    struct upipe *sink = &upipe_ts_mux->inner_sink;
    upipe_init(sink, inner_sink_mgr,
               uprobe_pfx_alloc(uprobe_use(upipe->uprobe), UPROBE_LOG_VERBOSE,
                                "inner sink"));
    sink->refcount = upipe_ts_mux_to_urefcount_real(upipe_ts_mux);
}

/** @internal @This cleans up the inner pseudo sink pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_clean_inner_sink(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct upipe *sink = &upipe_ts_mux->inner_sink;
    upipe_clean(sink);
}


/*
 * upipe_ts_mux structure handling (derived from upipe structure)
 */

/** @internal @This catches the events from inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_probe(struct uprobe *uprobe, struct upipe *inner,
                              int event, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux =
        container_of(uprobe, struct upipe_ts_mux, probe);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a ts_mux pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_mux_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_mux_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux_init_urefcount(upipe);
    urefcount_init(upipe_ts_mux_to_urefcount_real(upipe_ts_mux),
                   upipe_ts_mux_free);
    upipe_ts_mux_init_output(upipe);
    upipe_ts_mux_init_upump_mgr(upipe);
    upipe_ts_mux_init_upump(upipe);
    upipe_ts_mux_init_uref_mgr(upipe);
    upipe_ts_mux_init_ubuf_mgr(upipe);
    upipe_ts_mux_init_uclock(upipe);
    upipe_ts_mux_init_bin_input(upipe);
    upipe_ts_mux_init_inner_sink(upipe);
    upipe_ts_mux_init_program_mgr(upipe);
    upipe_ts_mux_init_sub_programs(upipe);

    upipe_ts_mux->live = false;
    upipe_ts_mux->psi_pid_pat = NULL;
    upipe_ts_mux->psig_nit = NULL;
    upipe_ts_mux->sig = NULL;
    upipe_ts_mux->psi_pid_nit = NULL;
    upipe_ts_mux->psi_pid_sdt = NULL;
    upipe_ts_mux->psi_pid_eit = NULL;
    upipe_ts_mux->psi_pid_tdt = NULL;
    upipe_ts_mux->padding = NULL;

    upipe_ts_mux->flow_def_input = NULL;
    upipe_ts_mux->auto_conformance = true;
    upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_ISO;
    upipe_ts_mux->pat_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pmt_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->nit_interval = DEFAULT_NIT_INTERVAL;
    upipe_ts_mux->sdt_interval = MAX_SDT_INTERVAL;
    upipe_ts_mux->eit_interval = MAX_EIT_INTERVAL;
    upipe_ts_mux->tdt_interval = MAX_TDT_INTERVAL;
    upipe_ts_mux->pcr_interval = DEFAULT_PCR_INTERVAL;
    upipe_ts_mux->scte35_interval = DEFAULT_SCTE35_INTERVAL;
    upipe_ts_mux->aac_encaps = DEFAULT_AAC_ENCAPS;
    upipe_ts_mux->encoding = DEFAULT_ENCODING;
    upipe_ts_mux->max_delay = UINT64_MAX;
    upipe_ts_mux->mux_delay = DEFAULT_MUX_DELAY;
    upipe_ts_mux->initial_cr_prog = UINT64_MAX;
    upipe_ts_mux->sid_auto = DEFAULT_SID_AUTO;
    upipe_ts_mux->pid_auto = DEFAULT_PID_AUTO;
    upipe_ts_mux->eits_octetrate = DEFAULT_EITS_OCTETRATE;
    upipe_ts_mux->fixed_octetrate = 0;
    upipe_ts_mux->padding_octetrate = 0;
    upipe_ts_mux->total_octetrate = 0;
    upipe_ts_mux->required_octetrate = 0;
    upipe_ts_mux->octetrate_in_progress = false;
    upipe_ts_mux->interval = 0;

    ulist_init(&upipe_ts_mux->psi_pids);
    ulist_init(&upipe_ts_mux->psi_pids_splice);
    ulist_init(&upipe_ts_mux->psi_inputs);
    upipe_ts_mux->mode = UPIPE_TS_MUX_MODE_CBR;
    upipe_ts_mux->tb_size = T_STD_TS_BUFFER;
    upipe_ts_mux->mtu = TS_SIZE;
    upipe_ts_mux->latency = 0;
    upipe_ts_mux->cr_sys = UINT64_MAX;
    upipe_ts_mux->cr_sys_remainder = 0;
    upipe_ts_mux->uref = NULL;
    upipe_ts_mux->uref_size = 0;
    upipe_ts_mux->preroll = true;

    uprobe_init(&upipe_ts_mux->probe, upipe_ts_mux_probe, NULL);
    upipe_ts_mux->probe.refcount = upipe_ts_mux_to_urefcount_real(upipe_ts_mux);

    upipe_throw_ready(upipe);

    upipe_ts_mux_require_uref_mgr(upipe);
    upipe_ts_mux_update(upipe);
    return upipe;
}

/** @internal @This returns whether the given SID already exists.
 *
 * @param upipe description structure of the pipe
 * @param sid SID to find
 * @return false if the SID doesn't exist
 */
static bool upipe_ts_mux_find_sid(struct upipe *upipe, uint16_t sid)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (!sid) /* NIT */
        return true;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->sid == sid)
            return true;
    }
    return false;
}

/** @internal @This returns whether the given PID already exists.
 *
 * @param upipe description structure of the pipe
 * @param pid PID to find
 * @return false if the PID doesn't exist
 */
static bool upipe_ts_mux_find_pid(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (!pid) /* PAT */
        return true;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->pmt_pid == pid)
            return true;

        struct uchain *uchain_input;
        ulist_foreach (&program->inputs, uchain_input) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            if (input->pid == pid)
                return true;
        }
    }
    return false;
}

/** @internal @This prepares PSI tables.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @param latency mux latency
 */
static void upipe_ts_mux_prepare_psi(struct upipe *upipe,
                                     uint64_t cr_sys, uint64_t latency)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (mux->psig != NULL)
        upipe_ts_mux_prepare(mux->psig, cr_sys, latency);
    if (mux->sig != NULL)
        upipe_ts_mux_prepare(mux->sig, cr_sys, latency);

    struct uchain *uchain;
    ulist_foreach (&mux->psi_inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain_psi(uchain);
        if (input->input != NULL)
            upipe_ts_mux_prepare(input->input, cr_sys, latency);
    }
}

/** @internal @This increments the cr_sys by a tick, and prepares PSI tables.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_increment(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    assert(mux->cr_sys != UINT64_MAX);
    lldiv_t q = lldiv((uint64_t)mux->mtu * UCLOCK_FREQ +
                      mux->cr_sys_remainder, mux->total_octetrate);
    mux->cr_sys += q.quot;
    mux->cr_sys_remainder = q.rem;

    /* Tell PSI tables to prepare packets. */
    upipe_ts_mux_prepare_psi(upipe, mux->cr_sys - mux->latency, mux->latency);
}

/** @internal @This shows the next increment of cr_sys.
 *
 * @param upipe description structure of the pipe
 * @return the next increment of cr_sys
 */
static uint64_t upipe_ts_mux_show_increment(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    assert(mux->cr_sys != UINT64_MAX);
    return mux->cr_sys +
        ((uint64_t)mux->mtu * UCLOCK_FREQ + mux->cr_sys_remainder) /
        mux->total_octetrate;
}

/** @internal @This splices a ubuf to output.
 *
 * @param upipe description structure of the pipe
 * @param ubuf_p filled in with the ubuf to output, or NULL if none is available
 * @param dts_sys_p filled with the dts_sys of the fragment
 */
static void upipe_ts_mux_splice(struct upipe *upipe, struct ubuf **ubuf_p,
                                uint64_t *dts_sys_p)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    uint64_t original_cr_sys = mux->cr_sys - mux->latency;
    struct uchain *uchain;
    int err;
    *ubuf_p = NULL;

    /* Order of priority: 1. PSI */
    while (!ulist_empty(&mux->psi_pids_splice)) {
        uchain = ulist_peek(&mux->psi_pids_splice);
        struct upipe_ts_mux_psi_pid *psi_pid =
            upipe_ts_mux_psi_pid_from_uchain_splice(uchain);

        if (psi_pid->dts_sys < original_cr_sys) {
            /* Too late: flush */
            upipe_ts_encaps_splice(psi_pid->encaps, original_cr_sys,
                                   original_cr_sys + mux->interval, NULL, NULL);
            /* No need to pop uchain as the probe does it for us. */
            continue;
        }

        if (psi_pid->cr_sys > original_cr_sys)
            break; /* Too soon */

        err = upipe_ts_encaps_splice(psi_pid->encaps, original_cr_sys,
                                     original_cr_sys + mux->interval,
                                     ubuf_p, dts_sys_p);
        if (!ubase_check(err)) {
            upipe_warn(upipe, "internal error in splice");
            upipe_throw_fatal(upipe, err);
        }
        /* No need to pop uchain as the probe does it for us. */
        return;
    }

    /* 2. Inputs */
    uint64_t cr_sys = UINT64_MAX;
    struct upipe_ts_mux_input *selected_input = NULL;
    struct uchain *uchain_tmp;
    ulist_delete_foreach (&mux->programs, uchain, uchain_tmp) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_use(upipe_ts_mux_program_to_upipe(program));

        struct uchain *uchain_input, *uchain_tmp2;
        ulist_delete_foreach (&program->inputs, uchain_input, uchain_tmp2) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            if (input->dts_sys < original_cr_sys) { /* flush */
                upipe_ts_encaps_splice(input->encaps, original_cr_sys,
                                       original_cr_sys + mux->interval,
                                       NULL, NULL);

                if (input->deleted && !input->ready) {
                    /* This triggers the immediate deletion of the input. */
                    upipe_release(input->encaps);
                    continue;
                }
            }

            if (input->dts_sys <= original_cr_sys + mux->interval ||
                input->pcr_sys <= original_cr_sys) {
                selected_input = input;
                upipe_release(upipe_ts_mux_program_to_upipe(program));
                goto upipe_ts_mux_splice_done;
            }
            if (input->cr_sys < cr_sys) {
                selected_input = input;
                cr_sys = input->cr_sys;
            }
        }

        upipe_release(upipe_ts_mux_program_to_upipe(program));
    }

    if (selected_input == NULL ||
        selected_input->cr_sys > original_cr_sys)
        return;

upipe_ts_mux_splice_done:
    err = upipe_ts_encaps_splice(selected_input->encaps, original_cr_sys,
                                 original_cr_sys + mux->interval,
                                 ubuf_p, dts_sys_p);
    if (!ubase_check(err)) {
        upipe_warn(upipe, "internal error in splice");
        upipe_throw_fatal(upipe, err);
    }

    if (selected_input->deleted && !selected_input->ready) {
        /* This triggers the immediate deletion of the input. */
        upipe_release(selected_input->encaps);
    }
}

/** @internal @This appends a uref to our buffer.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf to append
 * @param dts_sys dts_sys associated with the ubuf
 */
static void upipe_ts_mux_append(struct upipe *upipe, struct ubuf *ubuf,
                                uint64_t dts_sys)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (mux->uref == NULL) {
        mux->uref = uref_alloc(mux->uref_mgr);
        if (unlikely(mux->uref == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            ubuf_free(ubuf);
            return;
        }
        uref_clock_set_cr_sys(mux->uref, mux->cr_sys - mux->latency);
        if (dts_sys != UINT64_MAX)
            uref_clock_set_cr_dts_delay(mux->uref,
                    dts_sys - (mux->cr_sys - mux->latency));
        uref_attach_ubuf(mux->uref, ubuf);
    } else {
        uint64_t current_dts_sys;
        if (dts_sys != UINT64_MAX &&
            (!ubase_check(uref_clock_get_dts_sys(mux->uref,
                                                 &current_dts_sys)) ||
             current_dts_sys > dts_sys))
            uref_clock_set_cr_dts_delay(mux->uref,
                    dts_sys - (mux->cr_sys - mux->latency));
        uref_block_append(mux->uref, ubuf);
    }
    mux->uref_size += TS_SIZE;
}

/** @internal @This completes a uref and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_mux_complete(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    struct uref *uref = mux->uref;
    mux->uref = NULL;
    mux->uref_size = 0;
    upipe_ts_mux_output(upipe, uref, upump_p);
}

/** @internal @This runs when the pump expires (live mode only).
 *
 * @param upipe description structure of the pipe
 */
static void _upipe_ts_mux_watcher(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (unlikely(mux->cr_sys == UINT64_MAX))
        mux->cr_sys = uclock_now(mux->uclock);

    unsigned int nb_packets = 0;
    while (nb_packets < NB_PACKETS) {
        upipe_ts_mux_increment(upipe);
        if (mux->uref != NULL) /* capped VBR */
            uref_clock_set_cr_sys(mux->uref, mux->cr_sys - mux->latency);

        while (mux->uref_size < mux->mtu) {
            nb_packets++;
            struct ubuf *ubuf;
            uint64_t dts_sys;
            upipe_ts_mux_splice(upipe, &ubuf, &dts_sys);
            if (ubuf == NULL)
                break;
            upipe_ts_mux_append(upipe, ubuf, dts_sys);
        }

        uint64_t dts_sys;
        if (mux->mode != UPIPE_TS_MUX_MODE_CAPPED ||
            (mux->uref != NULL &&
             ubase_check(uref_clock_get_dts_sys(mux->uref, &dts_sys)) &&
             dts_sys + mux->latency < upipe_ts_mux_show_increment(upipe))) {
            while (mux->uref_size < mux->mtu) {
                nb_packets++;
                struct ubuf *ubuf = ubuf_dup(mux->padding);
                if (ubuf == NULL)
                    break;
                upipe_ts_mux_append(upipe, ubuf, UINT64_MAX);
            }
        }

        if (mux->uref_size >= mux->mtu)
            upipe_ts_mux_complete(upipe, &mux->upump);
    }

    upipe_ts_mux_set_upump(upipe, NULL);
    upipe_ts_mux_work(upipe, NULL);
}

/** @internal @This runs when the pump expires (live mode only).
 *
 * @param upump description structure of the pump
 */
static void upipe_ts_mux_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    _upipe_ts_mux_watcher(upipe);
}

/** @internal @This checks whether a packet is available on all inputs
 * (used in a file mode only).
 *
 * @param upipe description structure of the pipe
 * @return the lowest available date, or UINT64_MAX
 */
static uint64_t upipe_ts_mux_check_available(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    uint64_t min_cr_sys = UINT64_MAX;

    struct uchain *uchain_program, *uchain_program_tmp;
    ulist_delete_foreach (&mux->programs, uchain_program,
                          uchain_program_tmp) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        upipe_use(upipe_ts_mux_program_to_upipe(program));

        struct uchain *uchain_input, *uchain_input_tmp;
        ulist_delete_foreach (&program->inputs, uchain_input,
                              uchain_input_tmp) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            if (!input->ready) {
                if (input->deleted) {
                    upipe_release(input->encaps);
                    continue;
                } else if (input->input_type != UPIPE_TS_MUX_INPUT_OTHER &&
                           input->input_type != UPIPE_TS_MUX_INPUT_SCTE35 &&
                           (input->input_type != UPIPE_TS_MUX_INPUT_UNKNOWN ||
                            mux->preroll)) {
                    upipe_release(upipe_ts_mux_program_to_upipe(program));
                    return UINT64_MAX;
                }
            }
            if (min_cr_sys > input->cr_sys)
                min_cr_sys = input->cr_sys;
        }
        upipe_release(upipe_ts_mux_program_to_upipe(program));
    }
    return min_cr_sys;
}

/** @internal @This sets the initial cr_prog of all programs.
 *
 * @param upipe description structure of the pipe
 * @param cr_prog initial cr_prog value
 */
static void upipe_ts_mux_init_cr_prog(struct upipe *upipe, uint64_t cr_prog)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    struct uchain *uchain_program;
    ulist_foreach (&mux->programs, uchain_program) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        upipe_ts_mux_program_init_cr_prog(
                upipe_ts_mux_program_to_upipe(program), cr_prog);
    }
}

/** @internal @This checks if a packet must be output in file mode.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_mux_work_file(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    uint64_t min_cr_sys;
#ifdef DEBUG_FILE
    upipe_verbose(upipe, "work file starting");
#endif
    while ((min_cr_sys = upipe_ts_mux_check_available(upipe)) !=
            UINT64_MAX) {
#ifdef DEBUG_FILE
        upipe_verbose(upipe, "work file running");
#endif
        if (mux->cr_sys == UINT64_MAX) {
            upipe_verbose_va(upipe, "work file min=%"PRIu64, min_cr_sys);
            mux->cr_sys = min_cr_sys + mux->latency;
            if (mux->initial_cr_prog != UINT64_MAX) {
                upipe_ts_mux_init_cr_prog(upipe, mux->initial_cr_prog);
                mux->initial_cr_prog = UINT64_MAX;
            }
            upipe_ts_mux_prepare_psi(upipe, min_cr_sys, 0);
        }

        struct ubuf *ubuf;
        uint64_t dts_sys;
        upipe_ts_mux_splice(upipe, &ubuf, &dts_sys);
        if (ubuf != NULL) {
            upipe_ts_mux_append(upipe, ubuf, dts_sys);
            if (mux->uref_size >= mux->mtu) {
                upipe_ts_mux_complete(upipe, &mux->upump);
                upipe_ts_mux_increment(upipe);
            }
            continue;
        }

        if (mux->mode == UPIPE_TS_MUX_MODE_CAPPED &&
            (mux->uref == NULL ||
             !ubase_check(uref_clock_get_dts_sys(mux->uref, &dts_sys)) ||
             dts_sys + mux->latency >= upipe_ts_mux_show_increment(upipe))) {
            upipe_ts_mux_increment(upipe);
            continue;
        }

        while (mux->uref_size < mux->mtu) {
            struct ubuf *ubuf = ubuf_dup(mux->padding);
            if (ubuf == NULL)
                break;
            upipe_ts_mux_append(upipe, ubuf, UINT64_MAX);
        }

        upipe_ts_mux_complete(upipe, upump_p);
        upipe_ts_mux_increment(upipe);
    }
#ifdef DEBUG_FILE
    upipe_verbose(upipe, "work file leaving");
#endif
}

/** @internal @This checks if a packet must be output in live mode.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_mux_work_live(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (likely(mux->upump != NULL))
        return;

    upipe_ts_mux_check_upump_mgr(upipe);
    if (mux->upump_mgr == NULL)
        return;

    struct upump *upump = NULL;
    if (likely(mux->cr_sys != UINT64_MAX)) {
        uint64_t next_cr_sys = upipe_ts_mux_show_increment(upipe);
        uint64_t now = uclock_now(mux->uclock);
        if (next_cr_sys > now + mux->mux_delay) {
            upump = upump_alloc_timer(mux->upump_mgr, upipe_ts_mux_watcher,
                                      upipe, upipe->refcount,
                                      next_cr_sys - now - mux->mux_delay, 0);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return;
            }
        } else if (next_cr_sys > now) {
            _upipe_ts_mux_watcher(upipe);
            return;
        } else
            upipe_warn_va(upipe, "missed a tick by %"PRIu64" ms",
                          (now - next_cr_sys) * 1000 / UCLOCK_FREQ);
    }
    if (unlikely(upump == NULL)) {
        mux->cr_sys = UINT64_MAX;
        mux->cr_sys_remainder = 0;
        upump = upump_alloc_idler(mux->upump_mgr, upipe_ts_mux_watcher, upipe,
                                  upipe->refcount);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return;
        }
    }
    upump_start(upump);
    upipe_ts_mux_set_upump(upipe, upump);
}

/** @internal @This checks if a packet must be output.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_mux_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (unlikely(mux->flow_def == NULL || mux->padding == NULL ||
                 !mux->interval))
        return;

    if (!mux->live)
        upipe_ts_mux_work_file(upipe, upump_p);
    else
        upipe_ts_mux_work_live(upipe, upump_p);
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_mux_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (flow_format != NULL)
        uref_free(flow_format);

    if (mux->uref_mgr == NULL)
        return UBASE_ERR_NONE;

    if (mux->psi_pid_pat == NULL) {
        struct upipe_ts_mux_mgr *ts_mux_mgr =
            upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);

        mux->psi_pid_pat = upipe_ts_mux_psi_pid_use(upipe, PAT_PID);
        struct upipe *psig = NULL, *psi_join;
        if (unlikely(mux->psi_pid_pat == NULL ||
                     (psig = upipe_void_alloc(ts_mux_mgr->ts_psig_mgr,
                             uprobe_pfx_alloc(
                                 uprobe_use(&mux->probe),
                                 UPROBE_LOG_VERBOSE, "psig"))) == NULL ||
                     (psi_join =
                      upipe_void_alloc_output_sub(psig,
                             mux->psi_pid_pat->psi_join,
                             uprobe_pfx_alloc(
                                 uprobe_use(&mux->probe),
                                 UPROBE_LOG_VERBOSE,
                                 "pat psi_join"))) == NULL)) {
            upipe_ts_mux_store_bin_input(upipe, psig);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_ts_mux_store_bin_input(upipe, psig);
        upipe_release(psi_join);
        upipe_ts_mux_update(upipe);
    }

    if (mux->flow_def == NULL)
        upipe_ts_mux_build_flow_def(upipe);

    if (mux->ubuf_mgr == NULL) {
        struct uref *flow_def_dup =
            uref_block_flow_alloc_def(mux->uref_mgr, "mpegts.");
        if (unlikely(flow_def_dup == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_ts_mux_require_ubuf_mgr(upipe, flow_def_dup);
        return UBASE_ERR_NONE;
    }

    if (mux->padding == NULL) {
        mux->padding = ubuf_block_alloc(mux->ubuf_mgr, TS_SIZE);
        if (unlikely(mux->padding == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }

        uint8_t *buffer;
        int size = -1;
        if (unlikely(!ubase_check(ubuf_block_write(mux->padding, 0, &size,
                                                   &buffer)))) {
            ubuf_free(mux->padding);
            mux->padding = NULL;
            return UBASE_ERR_ALLOC;
        }
        assert(size == TS_SIZE);
        ts_pad(buffer);
        ubuf_block_unmap(mux->padding, 0);
    }

    if (mux->live && mux->sig != NULL)
        upipe_ts_mux_set_tdt_interval(mux->sig, mux->tdt_interval);

    upipe_ts_mux_work(upipe, NULL);
    return UBASE_ERR_NONE;
}

/** @This prints the current operating mode of the mux.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_notice(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (mux->live) {
        if (mux->total_octetrate == mux->required_octetrate)
            upipe_notice_va(upipe,
                    "now operating (live) in %s mode at %"PRIu64" bits/s (auto), conformance %s, latency %"PRIu64" ms",
                    upipe_ts_mux_mode_print(mux->mode),
                    mux->total_octetrate * 8,
                    upipe_ts_conformance_print(mux->conformance),
                    (mux->latency + mux->mux_delay) * 1000 / UCLOCK_FREQ);
        else
            upipe_notice_va(upipe,
                    "now operating (live) in %s mode at %"PRIu64" bits/s (requires %"PRIu64" bits/s), conformance %s, latency %"PRIu64" ms",
                    upipe_ts_mux_mode_print(mux->mode),
                    mux->total_octetrate * 8, mux->required_octetrate * 8,
                    upipe_ts_conformance_print(mux->conformance),
                    (mux->latency + mux->mux_delay) * 1000 / UCLOCK_FREQ);
    } else {
        if (mux->total_octetrate == mux->required_octetrate)
            upipe_notice_va(upipe,
                    "now operating (file) in %s mode at %"PRIu64" bits/s (auto), conformance %s",
                    upipe_ts_mux_mode_print(mux->mode),
                    mux->total_octetrate * 8,
                    upipe_ts_conformance_print(mux->conformance));
        else
            upipe_notice_va(upipe,
                    "now operating (file) in %s mode at %"PRIu64" bits/s (requires %"PRIu64" bits/s), conformance %s",
                    upipe_ts_mux_mode_print(mux->mode),
                    mux->total_octetrate * 8, mux->required_octetrate * 8,
                    upipe_ts_conformance_print(mux->conformance));
    }
}

/** @This calculates the total octetrate used by a stream and updates the
 * mux.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_update(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (mux->octetrate_in_progress)
        return;
    mux->octetrate_in_progress = true;

    uint64_t required_octetrate = mux->padding_octetrate;
    struct uchain *uchain;
    ulist_foreach (&mux->psi_pids, uchain) {
        struct upipe_ts_mux_psi_pid *psi_pid =
            upipe_ts_mux_psi_pid_from_uchain(uchain);
        required_octetrate += psi_pid->octetrate;
    }

    ulist_foreach (&mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        required_octetrate += program->required_octetrate;
    }

    /* Add a margin to take into account 1/ the drift of the input clock
     * 2/ the drift of the output clock. */
    required_octetrate +=
        (required_octetrate * PCR_TOLERANCE_PPM * 2 + 999999) / 1000000;
    /* Add a margin to avoid toggling between two values. */
    required_octetrate += TS_SIZE;
    required_octetrate -= required_octetrate % TS_SIZE;
    mux->required_octetrate = required_octetrate;

    if (mux->live && mux->mux_delay &&
        required_octetrate < mux->mtu * UCLOCK_FREQ / mux->mux_delay)
        required_octetrate = mux->mtu * UCLOCK_FREQ / mux->mux_delay;

    uint64_t total_octetrate = mux->fixed_octetrate;
    if (!total_octetrate)
        total_octetrate = required_octetrate;

    if (total_octetrate != mux->total_octetrate) {
        mux->total_octetrate = total_octetrate;
        upipe_ts_mux_build_flow_def(upipe);
        upipe_ts_mux_set_upump(upipe, NULL);
    }

    upipe_ts_mux_notice(upipe);

    if (mux->total_octetrate) {
        mux->interval = (mux->mtu * UCLOCK_FREQ + mux->total_octetrate - 1) /
                        mux->total_octetrate;
        upipe_ts_mux_set_pat_interval(mux->psig,
                mux->interval < mux->pat_interval / 2 ?
                mux->pat_interval - (mux->pat_interval % mux->interval) :
                mux->pat_interval);

        if (mux->sig != NULL) {
            upipe_ts_mux_set_nit_interval(mux->sig,
                    mux->interval < mux->nit_interval / 2 ?
                    mux->nit_interval - (mux->nit_interval % mux->interval) :
                    mux->nit_interval);
            upipe_ts_mux_set_sdt_interval(mux->sig,
                    mux->interval < mux->sdt_interval / 2 ?
                    mux->sdt_interval - (mux->sdt_interval % mux->interval) :
                    mux->sdt_interval);
            if (mux->live)
                upipe_ts_mux_set_tdt_interval(mux->sig,
                        mux->interval < mux->tdt_interval / 2 ?
                        mux->tdt_interval - (mux->tdt_interval % mux->interval) :
                        mux->tdt_interval);
        }

        struct uchain *uchain_program;
        ulist_foreach (&mux->programs, uchain_program) {
            struct upipe_ts_mux_program *program =
                upipe_ts_mux_program_from_uchain(uchain_program);
            upipe_ts_mux_set_pmt_interval(program->psig_program,
                    mux->interval < program->pmt_interval / 2 ?
                    program->pmt_interval -
                    (program->pmt_interval % mux->interval) :
                    program->pmt_interval);

            if (program->sig_service != NULL) {
                upipe_ts_mux_set_eit_interval(program->sig_service,
                        mux->interval < program->eit_interval / 2 ?
                        program->eit_interval -
                        (program->eit_interval % mux->interval) :
                        program->eit_interval);
            }

            struct uchain *uchain_input;
            ulist_foreach (&program->inputs, uchain_input) {
                struct upipe_ts_mux_input *input =
                    upipe_ts_mux_input_from_uchain(uchain_input);
                if (input->pcr && input->encaps != NULL)
                    upipe_ts_mux_set_pcr_interval(input->encaps,
                            mux->interval < program->pcr_interval / 2 ?
                            program->pcr_interval -
                            (program->pcr_interval % mux->interval) :
                            program->pcr_interval);
            }
        }
    }
    mux->octetrate_in_progress = false;
}

/** @internal @This builds the output flow definition.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (mux->uref_mgr == NULL)
        return;

    struct uref *flow_def =
        uref_block_flow_alloc_def(mux->uref_mgr, "mpegtsaligned.");
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (unlikely(!ubase_check(uref_clock_set_latency(flow_def,
                                mux->latency + mux->mux_delay)) ||
                 !ubase_check(uref_block_flow_set_octetrate(flow_def,
                                mux->total_octetrate)) ||
                 !ubase_check(uref_block_flow_set_size(flow_def,
                                mux->mtu))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_ts_mux_store_flow_def(upipe, flow_def);

    struct uchain *uchain_program;
    ulist_foreach (&mux->programs, uchain_program) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        struct uchain *uchain_input;
        ulist_foreach (&program->inputs, uchain_input) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            if (input->encaps != NULL) {
                if (mux->live && input->original_au_per_sec.den)
                    /* live mode */
                    upipe_set_max_length(input->encaps,
                            (MIN_BUFFERING + mux->latency) *
                            input->original_au_per_sec.num /
                            input->original_au_per_sec.den / UCLOCK_FREQ);
                else /* file mode */
                    upipe_set_max_length(input->encaps, UINT_MAX);
            }
        }
    }
}

/** @internal @This builds an appropriate flow definition packet, and
 * sends it to generators.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_conformance_flow_def(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (upipe_ts_mux->flow_def_input == NULL)
        return;

    if (upipe_ts_conformance_from_flow_def(upipe_ts_mux->flow_def_input) ==
        upipe_ts_mux->conformance) {
        upipe_set_flow_def(upipe_ts_mux->psig, upipe_ts_mux->flow_def_input);
        if (upipe_ts_mux->sig != NULL)
            upipe_set_flow_def(upipe_ts_mux->sig, upipe_ts_mux->flow_def_input);
        return;
    }

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(upipe_ts_mux->flow_def_input)) ==
                 NULL ||
                 !ubase_check(upipe_ts_conformance_to_flow_def(flow_def_dup,
                         upipe_ts_mux->conformance)))) {
        uref_free(flow_def_dup);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_set_flow_def(upipe_ts_mux->psig, flow_def_dup);
    if (upipe_ts_mux->sig != NULL)
        upipe_set_flow_def(upipe_ts_mux->sig, flow_def_dup);
    uref_free(flow_def_dup);
}

/** @internal @This updates the PSI generator.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_update_psig(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    /* Fix up PSI and PCR intervals */
    uint64_t max_psi_interval = UINT64_MAX;
    uint64_t max_pcr_interval = MAX_PCR_INTERVAL_ISO;
    switch (upipe_ts_mux->conformance) {
        case UPIPE_TS_CONFORMANCE_DVB:
        case UPIPE_TS_CONFORMANCE_DVB_NO_TABLES:
        case UPIPE_TS_CONFORMANCE_ISDB:
            max_psi_interval = MAX_PSI_INTERVAL_DVB;
            max_pcr_interval = MAX_PCR_INTERVAL_DVB;
            break;
        case UPIPE_TS_CONFORMANCE_ATSC:
            max_psi_interval = MAX_PSI_INTERVAL_ATSC;
            break;
        default:
            break;
    }

    if (upipe_ts_mux->pat_interval > max_psi_interval)
        upipe_ts_mux->pat_interval = max_psi_interval;
    if (upipe_ts_mux->pmt_interval > max_psi_interval)
        upipe_ts_mux->pmt_interval = max_psi_interval;
    if (upipe_ts_mux->pcr_interval > max_pcr_interval)
        upipe_ts_mux->pcr_interval = max_pcr_interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->pmt_interval > max_psi_interval)
            program->pmt_interval = max_psi_interval;
        if (program->pcr_interval > max_pcr_interval)
            program->pcr_interval = max_pcr_interval;
    }
}

/** @internal @This updates the SI generator.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_update_sig(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (mux->conformance != UPIPE_TS_CONFORMANCE_DVB &&
        mux->conformance != UPIPE_TS_CONFORMANCE_ISDB) {
        if (mux->sig == NULL)
            return;

        upipe_release(mux->psig_nit);
        mux->psig_nit = NULL;
        upipe_release(mux->sig);
        mux->sig = NULL;
        upipe_ts_mux_psi_pid_release(mux->psi_pid_nit);
        mux->psi_pid_nit = NULL;
        upipe_ts_mux_psi_pid_release(mux->psi_pid_sdt);
        mux->psi_pid_sdt = NULL;
        upipe_ts_mux_psi_pid_release(mux->psi_pid_eit);
        mux->psi_pid_eit = NULL;
        upipe_ts_mux_psi_pid_release(mux->psi_pid_tdt);
        mux->psi_pid_tdt = NULL;

        struct uchain *uchain;
        ulist_foreach (&mux->programs, uchain) {
            struct upipe_ts_mux_program *program =
                upipe_ts_mux_program_from_uchain(uchain);
            upipe_ts_mux_program_update_sig(upipe_ts_mux_program_to_upipe(program));
        }
        return;
    }

    if (mux->sig != NULL)
        return;

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);

    mux->psi_pid_nit = upipe_ts_mux_psi_pid_use(upipe, NIT_PID);
    mux->psi_pid_sdt = upipe_ts_mux_psi_pid_use(upipe, SDT_PID);
    mux->psi_pid_eit = upipe_ts_mux_psi_pid_use(upipe, EIT_PID);
    mux->psi_pid_tdt = upipe_ts_mux_psi_pid_use(upipe, TDT_PID);
    struct upipe *nit, *sdt, *eit, *tdt;
    if (unlikely(mux->psi_pid_nit == NULL ||
                 mux->psi_pid_sdt == NULL ||
                 mux->psi_pid_eit == NULL ||
                 mux->psi_pid_tdt == NULL ||
                 (mux->sig = upipe_ts_sig_alloc(ts_mux_mgr->ts_sig_mgr,
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "sig"),
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "sig nit"),
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "sig sdt"),
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "sig eit"),
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "sig tdt"))) == NULL ||
                 !ubase_check(upipe_ts_sig_get_nit_sub(mux->sig, &nit)) ||
                 !ubase_check(upipe_ts_sig_get_sdt_sub(mux->sig, &sdt)) ||
                 !ubase_check(upipe_ts_sig_get_eit_sub(mux->sig, &eit)) ||
                 !ubase_check(upipe_ts_sig_get_tdt_sub(mux->sig, &tdt)) ||
                 !ubase_check(upipe_void_spawn_output_sub(nit,
                         mux->psi_pid_nit->psi_join,
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "nit psi_join"))) ||
                 !ubase_check(upipe_void_spawn_output_sub(sdt,
                         mux->psi_pid_sdt->psi_join,
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "sdt psi_join"))) ||
                 !ubase_check(upipe_void_spawn_output_sub(eit,
                         mux->psi_pid_eit->psi_join,
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "eit psi_join"))) ||
                 !ubase_check(upipe_void_spawn_output_sub(tdt,
                         mux->psi_pid_tdt->psi_join,
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "tdt psi_join"))))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_mux_set_encoding(mux->sig, mux->encoding);
    upipe_ts_mux_set_eits_octetrate(mux->sig, mux->eits_octetrate);

    struct uchain *uchain;
    ulist_foreach (&mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_program_update_sig(upipe_ts_mux_program_to_upipe(program));
    }

    /* prepare NIT entry in the PAT */
    struct uref *flow_def = uref_alloc_control(mux->uref_mgr);
    if (unlikely(flow_def == NULL ||
                 !ubase_check(uref_flow_set_def(flow_def, "void.")) ||
                 !ubase_check(uref_flow_set_id(flow_def, 0)) ||
                 !ubase_check(uref_ts_flow_set_pid(flow_def, NIT_PID)) ||
                 (mux->psig_nit = upipe_void_alloc_sub(mux->psig,
                         uprobe_pfx_alloc(uprobe_use(&mux->probe),
                             UPROBE_LOG_VERBOSE, "psig nit"))) == NULL ||
                 !ubase_check(upipe_set_flow_def(mux->psig_nit, flow_def)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_free(flow_def);
}

/** @internal @This changes the current conformance, and starts necessary
 * generators.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 */
static void upipe_ts_mux_conformance_change(struct upipe *upipe,
        enum upipe_ts_conformance conformance)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (upipe_ts_mux->conformance == conformance)
        return;

    upipe_ts_mux->conformance = conformance;

    upipe_ts_mux_update_psig(upipe);
    upipe_ts_mux_update_sig(upipe);
    upipe_ts_mux_update(upipe);
    upipe_ts_mux_conformance_flow_def(upipe);
}

/** @internal @This tries to guess the conformance of the stream from the
 * information that is available to us.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_conformance_guess(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (!upipe_ts_mux->auto_conformance)
        return;
    if (upipe_ts_mux->flow_def_input == NULL)
        upipe_ts_mux_conformance_change(upipe,
                UPIPE_TS_CONFORMANCE_DVB_NO_TABLES);
    else
        upipe_ts_mux_conformance_change(upipe,
            upipe_ts_conformance_from_flow_def(upipe_ts_mux->flow_def_input));
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_mux_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    uref_free(upipe_ts_mux->flow_def_input);
    upipe_ts_mux->flow_def_input = flow_def_dup;
    upipe_ts_mux_conformance_guess(upipe);
    upipe_ts_mux_conformance_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @return an error code
 */
static int upipe_ts_mux_get_output_size(struct upipe *upipe,
                                        unsigned int *mtu_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(mtu_p != NULL);
    *mtu_p = upipe_ts_mux->mtu;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu configured mtu, in octets
 * @return an error code
 */
static int upipe_ts_mux_set_output_size(struct upipe *upipe, unsigned int mtu)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (unlikely(mtu < TS_SIZE))
        return UBASE_ERR_INVALID;
    mtu -= mtu % TS_SIZE;
    upipe_ts_mux->mtu = mtu;
    if (upipe_ts_mux->total_octetrate)
        upipe_ts_mux->interval = (upipe_ts_mux->mtu * UCLOCK_FREQ +
                                  upipe_ts_mux->total_octetrate - 1) /
                                 upipe_ts_mux->total_octetrate;

    upipe_ts_mux->tb_size = T_STD_TS_BUFFER + mtu - TS_SIZE;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->psi_pids, uchain) {
        struct upipe_ts_mux_psi_pid *psi_pid =
            upipe_ts_mux_psi_pid_from_uchain(uchain);
        upipe_ts_encaps_set_tb_size(psi_pid->encaps, upipe_ts_mux->tb_size);
    }

    struct uchain *uchain_program;
    ulist_foreach (&upipe_ts_mux->programs, uchain_program) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        struct uchain *uchain_input;
        ulist_foreach (&program->inputs, uchain_input) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            upipe_ts_encaps_set_tb_size(input->encaps,
                                        upipe_ts_mux->tb_size);
        }
    }
    upipe_ts_mux_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This ends the preroll period.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_ts_mux_end_preroll(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->preroll = false;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current conformance mode. It cannot
 * return CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return an error code
 */
static int _upipe_ts_mux_get_conformance(struct upipe *upipe,
                                  enum upipe_ts_conformance *conformance_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(conformance_p != NULL);
    *conformance_p = upipe_ts_mux->conformance;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the conformance mode.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 * @return an error code
 */
static int _upipe_ts_mux_set_conformance(struct upipe *upipe,
                                         enum upipe_ts_conformance conformance)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    switch (conformance) {
        case UPIPE_TS_CONFORMANCE_AUTO:
            upipe_ts_mux->auto_conformance = true;
            upipe_ts_mux_conformance_guess(upipe);
            break;
        case UPIPE_TS_CONFORMANCE_ISO:
        case UPIPE_TS_CONFORMANCE_DVB:
        case UPIPE_TS_CONFORMANCE_DVB_NO_TABLES:
        case UPIPE_TS_CONFORMANCE_ATSC:
        case UPIPE_TS_CONFORMANCE_ISDB:
            upipe_ts_mux->auto_conformance = false;
            upipe_ts_mux_conformance_change(upipe, conformance);
            break;
        default:
            return UBASE_ERR_INVALID;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_pat_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pat_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_pat_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->pat_interval = interval;
    upipe_ts_mux_update(upipe); /* will trigger set_pat_interval */
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_pmt_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pmt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_pmt_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->pmt_interval = interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_set_pmt_interval(upipe_ts_mux_program_to_upipe(program),
                                      interval);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current NIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_nit_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->nit_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the NIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_nit_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->nit_interval = interval;
    upipe_ts_mux_update(upipe); /* will trigger set_nit_interval */
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current SDT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_sdt_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->sdt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the SDT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_sdt_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->sdt_interval = interval;
    upipe_ts_mux_update(upipe); /* will trigger set_sdt_interval */
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current EIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_eit_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->eit_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the EIT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_eit_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->eit_interval = interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_set_eit_interval(upipe_ts_mux_program_to_upipe(program),
                                      interval);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current TDT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_tdt_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->tdt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the TDT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_tdt_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    mux->tdt_interval = interval;
    upipe_ts_mux_update(upipe); /* will trigger set_tdt_interval */
    if (!mux->live) {
        mux->live = true;
        upipe_ts_mux_require_uclock(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_pcr_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pcr_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_pcr_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->pcr_interval = interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_set_pcr_interval(upipe_ts_mux_program_to_upipe(program),
                                      interval);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int _upipe_ts_mux_get_scte35_interval(struct upipe *upipe,
                                             uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->scte35_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the SCTE35 interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int _upipe_ts_mux_set_scte35_interval(struct upipe *upipe,
                                             uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->scte35_interval = interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_set_scte35_interval(upipe_ts_mux_program_to_upipe(program),
                                         interval);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled in with the delay
 * @return an error code
 */
static int _upipe_ts_mux_get_max_delay(struct upipe *upipe, uint64_t *delay_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(delay_p != NULL);
    *delay_p = upipe_ts_mux->max_delay;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return an error code
 */
static int _upipe_ts_mux_set_max_delay(struct upipe *upipe, uint64_t delay)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->max_delay = delay;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_set_max_delay(upipe_ts_mux_program_to_upipe(program),
                                   delay);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current mux delay (live mode).
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled in with the delay
 * @return an error code
 */
static int _upipe_ts_mux_get_mux_delay(struct upipe *upipe, uint64_t *delay_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(delay_p != NULL);
    *delay_p = upipe_ts_mux->mux_delay;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the mux delay (live mode).
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return an error code
 */
static int _upipe_ts_mux_set_mux_delay(struct upipe *upipe, uint64_t delay)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->mux_delay = delay;
    upipe_ts_mux_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the initial cr_prog.
 *
 * @param upipe description structure of the pipe
 * @param cr_prog initial cr_prog
 * @return an error code
 */
static int _upipe_ts_mux_set_cr_prog(struct upipe *upipe, uint64_t cr_prog)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (upipe_ts_mux->cr_sys != UINT64_MAX)
        return UBASE_ERR_BUSY;
    upipe_ts_mux->initial_cr_prog = cr_prog;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current padding octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static int _upipe_ts_mux_get_padding_octetrate(struct upipe *upipe,
                                               uint64_t *octetrate_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(octetrate_p != NULL);
    *octetrate_p = upipe_ts_mux->padding_octetrate;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the padding octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static int _upipe_ts_mux_set_padding_octetrate(struct upipe *upipe,
                                               uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->padding_octetrate = octetrate;
    upipe_ts_mux_update(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static int _upipe_ts_mux_get_octetrate(struct upipe *upipe,
                                       uint64_t *octetrate_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(octetrate_p != NULL);
    *octetrate_p = upipe_ts_mux->total_octetrate;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static int _upipe_ts_mux_set_octetrate(struct upipe *upipe, uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->fixed_octetrate = octetrate;
    upipe_ts_mux_update(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current EITs octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static int _upipe_ts_mux_get_eits_octetrate(struct upipe *upipe,
                                            uint64_t *octetrate_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(octetrate_p != NULL);
    *octetrate_p = upipe_ts_mux->eits_octetrate;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the EITs octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static int _upipe_ts_mux_set_eits_octetrate(struct upipe *upipe,
                                            uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->eits_octetrate = octetrate;

    if (upipe_ts_mux->sig != NULL)
        upipe_ts_mux_set_eits_octetrate(upipe_ts_mux->sig, octetrate);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current mode.
 *
 * @param upipe description structure of the pipe
 * @param mode_p filled in with the mode
 * @return an error code
 */
static int _upipe_ts_mux_get_mode(struct upipe *upipe,
                                  enum upipe_ts_mux_mode *mode_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(mode_p != NULL);
    *mode_p = upipe_ts_mux->mode;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the mode.
 *
 * @param upipe description structure of the pipe
 * @param mode new mode
 * @return an error code
 */
static int _upipe_ts_mux_set_mode(struct upipe *upipe,
                                  enum upipe_ts_mux_mode mode)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->mode = mode;
    upipe_ts_mux_notice(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current encapsulation for AAC streams.
 *
 * @param upipe description structure of the pipe
 * @param encaps_p filled in with the encapsulation
 * @return an error code
 */
static int _upipe_ts_mux_get_aac_encaps(struct upipe *upipe, int *encaps_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(encaps_p != NULL);
    *encaps_p = upipe_ts_mux->aac_encaps;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the encapsulation for AAC streams.
 *
 * @param upipe description structure of the pipe
 * @param encaps encapsulation
 * @return an error code
 */
static int _upipe_ts_mux_set_aac_encaps(struct upipe *upipe, int encaps)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->aac_encaps = encaps;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        upipe_ts_mux_set_aac_encaps(upipe_ts_mux_program_to_upipe(program),
                                    encaps);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current encoding.
 *
 * @param upipe description structure of the pipe
 * @param encoding_p filled in with the encoding
 * @return an error code
 */
static int _upipe_ts_mux_get_encoding(struct upipe *upipe,
                                      const char **encoding_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(encoding_p != NULL);
    *encoding_p = upipe_ts_mux->encoding;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the encoding.
 *
 * @param upipe description structure of the pipe
 * @param encoding encoding
 * @return an error code
 */
static int _upipe_ts_mux_set_encoding(struct upipe *upipe, const char *encoding)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->encoding = encoding;

    if (upipe_ts_mux->sig != NULL)
        upipe_ts_mux_set_encoding(upipe_ts_mux->sig, encoding);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts_mux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_ts_mux_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_mux_control_programs(upipe, command, args));

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_ts_mux_set_upump(upipe, NULL);
            return upipe_ts_mux_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK: {
            struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
            mux->live = true;
            upipe_ts_mux_set_upump(upipe, NULL);
            upipe_ts_mux_require_uclock(upipe);
            upipe_ts_mux_update(upipe);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_ts_mux_control_output(upipe, command, args);
        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *mtu_p = va_arg(args, unsigned int *);
            return upipe_ts_mux_get_output_size(upipe, mtu_p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int mtu = va_arg(args, unsigned int);
            return upipe_ts_mux_set_output_size(upipe, mtu);
        }
        case UPIPE_END_PREROLL:
            return upipe_ts_mux_end_preroll(upipe);

        case UPIPE_TS_MUX_GET_CONFORMANCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            enum upipe_ts_conformance *conformance_p =
                va_arg(args, enum upipe_ts_conformance *);
            return _upipe_ts_mux_get_conformance(upipe, conformance_p);
        }
        case UPIPE_TS_MUX_SET_CONFORMANCE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            enum upipe_ts_conformance conformance =
                va_arg(args, enum upipe_ts_conformance);
            return _upipe_ts_mux_set_conformance(upipe, conformance);
        }
        case UPIPE_TS_MUX_GET_PAT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_pat_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PAT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_pat_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_pmt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_pmt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_NIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_nit_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_NIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_nit_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_SDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_sdt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_SDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_sdt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_EIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_eit_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_EIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_eit_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_TDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_tdt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_TDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_tdt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PCR_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_pcr_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PCR_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_pcr_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_scte35_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_SCTE35_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_scte35_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_MAX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *delay_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_max_delay(upipe, delay_p);
        }
        case UPIPE_TS_MUX_SET_MAX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t delay = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_max_delay(upipe, delay);
        }
        case UPIPE_TS_MUX_GET_MUX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *delay_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_mux_delay(upipe, delay_p);
        }
        case UPIPE_TS_MUX_SET_MUX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t delay = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_mux_delay(upipe, delay);
        }
        case UPIPE_TS_MUX_SET_CR_PROG: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t cr_prog = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_cr_prog(upipe, cr_prog);
        }
        case UPIPE_TS_MUX_GET_PADDING_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *octetrate_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_padding_octetrate(upipe, octetrate_p);
        }
        case UPIPE_TS_MUX_SET_PADDING_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t octetrate = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_padding_octetrate(upipe, octetrate);
        }
        case UPIPE_TS_MUX_GET_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *octetrate_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_octetrate(upipe, octetrate_p);
        }
        case UPIPE_TS_MUX_SET_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t octetrate = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_octetrate(upipe, octetrate);
        }
        case UPIPE_TS_MUX_GET_EITS_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *octetrate_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_eits_octetrate(upipe, octetrate_p);
        }
        case UPIPE_TS_MUX_SET_EITS_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t octetrate = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_eits_octetrate(upipe, octetrate);
        }
        case UPIPE_TS_MUX_GET_MODE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            enum upipe_ts_mux_mode *mode_p = va_arg(args,
                                                    enum upipe_ts_mux_mode *);
            return _upipe_ts_mux_get_mode(upipe, mode_p);
        }
        case UPIPE_TS_MUX_SET_MODE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            enum upipe_ts_mux_mode mode = va_arg(args, enum upipe_ts_mux_mode);
            return _upipe_ts_mux_set_mode(upipe, mode);
        }
        case UPIPE_TS_MUX_GET_AAC_ENCAPS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            int *encaps_p = va_arg(args, int *);
            return _upipe_ts_mux_get_aac_encaps(upipe, encaps_p);
        }
        case UPIPE_TS_MUX_SET_AAC_ENCAPS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            int encaps = va_arg(args, int);
            return _upipe_ts_mux_set_aac_encaps(upipe, encaps);
        }
        case UPIPE_TS_MUX_GET_ENCODING: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            const char **encoding_p = va_arg(args, const char **);
            return _upipe_ts_mux_get_encoding(upipe, encoding_p);
        }
        case UPIPE_TS_MUX_SET_ENCODING: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            const char *encoding = va_arg(args, const char *);
            return _upipe_ts_mux_set_encoding(upipe, encoding);
        }

        case UPIPE_TS_MUX_GET_VERSION:
        case UPIPE_TS_MUX_SET_VERSION:
        case UPIPE_TS_MUX_FREEZE_PSI: {
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux->psig, command, args);
        }

        case UPIPE_TS_MUX_GET_CC:
        case UPIPE_TS_MUX_SET_CC: {
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
            if (upipe_ts_mux->psi_pid_pat == NULL)
                return UBASE_ERR_UNHANDLED;
            return upipe_control_va(upipe_ts_mux->psi_pid_pat->encaps,
                                    command, args);
        }

        default:
            break;
    }

    return upipe_ts_mux_control_bin_input(upipe, command, args);
}

/** @internal @This processes control commands on a ts_mux pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mux_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_ts_mux_control(upipe, command, args));

    return upipe_ts_mux_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_mux_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_mux_to_upipe(mux);

    if (mux->uref != NULL) {
        size_t uref_size;
        while ((ubase_check(uref_block_size(mux->uref, &uref_size)) &&
                uref_size < mux->mtu)) {
            struct ubuf *ubuf = ubuf_dup(mux->padding);
            if (ubuf == NULL)
                break;
            upipe_ts_mux_append(upipe, ubuf, UINT64_MAX);
        }

        upipe_ts_mux_complete(upipe, NULL);
    }

    upipe_throw_dead(upipe);

    ubuf_free(mux->padding);
    uref_free(mux->flow_def_input);
    uprobe_clean(&mux->probe);
    urefcount_clean(urefcount_real);
    upipe_ts_mux_clean_inner_sink(upipe);
    upipe_ts_mux_clean_upump(upipe);
    upipe_ts_mux_clean_upump_mgr(upipe);
    upipe_ts_mux_clean_uref_mgr(upipe);
    upipe_ts_mux_clean_ubuf_mgr(upipe);
    upipe_ts_mux_clean_uclock(upipe);
    upipe_ts_mux_clean_output(upipe);
    upipe_ts_mux_clean_urefcount(upipe);
    upipe_ts_mux_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_no_input(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);

    upipe_ts_mux_clean_sub_programs(upipe);
    upipe_ts_mux_clean_bin_input(upipe);
    upipe_release(upipe_ts_mux->psig_nit);
    upipe_ts_mux->psig_nit = NULL;
    upipe_ts_mux_psi_pid_release(upipe_ts_mux->psi_pid_pat);
    upipe_ts_mux->psi_pid_pat = NULL;
    upipe_release(upipe_ts_mux->sig);
    upipe_ts_mux->sig = NULL;
    upipe_ts_mux_psi_pid_release(upipe_ts_mux->psi_pid_nit);
    upipe_ts_mux->psi_pid_nit = NULL;
    upipe_ts_mux_psi_pid_release(upipe_ts_mux->psi_pid_sdt);
    upipe_ts_mux->psi_pid_sdt = NULL;
    upipe_ts_mux_psi_pid_release(upipe_ts_mux->psi_pid_eit);
    upipe_ts_mux->psi_pid_eit = NULL;
    upipe_ts_mux_psi_pid_release(upipe_ts_mux->psi_pid_tdt);
    upipe_ts_mux->psi_pid_tdt = NULL;
    urefcount_release(upipe_ts_mux_to_urefcount_real(upipe_ts_mux));
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_ts_mux_mgr_free(struct urefcount *urefcount)
{
    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_urefcount(urefcount);
    upipe_mgr_release(ts_mux_mgr->ts_encaps_mgr);
    upipe_mgr_release(ts_mux_mgr->ts_tstd_mgr);
    upipe_mgr_release(ts_mux_mgr->ts_psi_join_mgr);
    upipe_mgr_release(ts_mux_mgr->ts_psig_mgr);
    upipe_mgr_release(ts_mux_mgr->ts_sig_mgr);
    upipe_mgr_release(ts_mux_mgr->ts_scte35g_mgr);

    urefcount_clean(urefcount);
    free(ts_mux_mgr);
}

/** @This processes control commands on a ts_mux manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mux_mgr_control(struct upipe_mgr *mgr,
                                    int command, va_list args)
{
    struct upipe_ts_mux_mgr *ts_mux_mgr = upipe_ts_mux_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_TS_MUX_MGR_GET_##NAME##_MGR: {                           \
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)             \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = ts_mux_mgr->name##_mgr;                                    \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_TS_MUX_MGR_SET_##NAME##_MGR: {                           \
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)             \
            if (!urefcount_single(&ts_mux_mgr->urefcount))                  \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(ts_mux_mgr->name##_mgr);                      \
            ts_mux_mgr->name##_mgr = upipe_mgr_use(m);                      \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(ts_encaps, TS_ENCAPS)
        GET_SET_MGR(ts_tstd, TS_TSTD)
        GET_SET_MGR(ts_psi_join, TS_PSI_JOIN)
        GET_SET_MGR(ts_psig, TS_PSIG)
        GET_SET_MGR(ts_sig, TS_SIG)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns a description string for local commands.
 *
 * @param cmd control command
 * @return description string
 */
const char *upipe_ts_mux_command_str(int cmd)
{
    switch (cmd) {
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_CONFORMANCE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_CONFORMANCE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_CC);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_CC);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_CR_PROG);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_PAT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_PAT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_PMT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_PMT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_NIT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_NIT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_SDT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_SDT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_EIT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_EIT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_TDT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_TDT_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_PCR_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_PCR_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_SCTE35_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_SCTE35_INTERVAL);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_MAX_DELAY);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_MAX_DELAY);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_MUX_DELAY);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_MUX_DELAY);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_OCTETRATE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_OCTETRATE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_PADDING_OCTETRATE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_PADDING_OCTETRATE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_MODE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_MODE);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_VERSION);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_VERSION);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_AAC_ENCAPS);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_AAC_ENCAPS);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_GET_ENCODING);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_SET_ENCODING);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_FREEZE_PSI);
        UBASE_CASE_TO_STR(UPIPE_TS_MUX_PREPARE);
        default: break;
    }
    return NULL;
}

/** @This returns a description string for local events.
 *
 * @param event event
 * @return description string
 */
const char *upipe_ts_mux_event_str(int event)
{
    switch (event) {
        UBASE_CASE_TO_STR(UPROBE_TS_MUX_LAST_CC);
        default: break;
    }
    return NULL;
}

/** @This returns the management structure for all ts_mux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_mux_mgr_alloc(void)
{
    struct upipe_ts_mux_mgr *ts_mux_mgr =
        malloc(sizeof(struct upipe_ts_mux_mgr));
    if (unlikely(ts_mux_mgr == NULL))
        return NULL;

    memset(ts_mux_mgr, 0, sizeof(*ts_mux_mgr));
    ts_mux_mgr->ts_encaps_mgr = upipe_ts_encaps_mgr_alloc();
    ts_mux_mgr->ts_tstd_mgr = upipe_ts_tstd_mgr_alloc();
    ts_mux_mgr->ts_psi_join_mgr = upipe_ts_psi_join_mgr_alloc();
    ts_mux_mgr->ts_psig_mgr = upipe_ts_psig_mgr_alloc();
    ts_mux_mgr->ts_sig_mgr = upipe_ts_sig_mgr_alloc();
    ts_mux_mgr->ts_scte35g_mgr = upipe_ts_scte35g_mgr_alloc();

    urefcount_init(upipe_ts_mux_mgr_to_urefcount(ts_mux_mgr),
                   upipe_ts_mux_mgr_free);
    ts_mux_mgr->mgr.refcount = upipe_ts_mux_mgr_to_urefcount(ts_mux_mgr);
    ts_mux_mgr->mgr.signature = UPIPE_TS_MUX_SIGNATURE;
    ts_mux_mgr->mgr.upipe_command_str = upipe_ts_mux_command_str;
    ts_mux_mgr->mgr.upipe_event_str = upipe_ts_mux_event_str;
    ts_mux_mgr->mgr.upipe_alloc = upipe_ts_mux_alloc;
    ts_mux_mgr->mgr.upipe_input = NULL;
    ts_mux_mgr->mgr.upipe_control = upipe_ts_mux_control;
    ts_mux_mgr->mgr.upipe_mgr_control = upipe_ts_mux_mgr_control;
    return upipe_ts_mux_mgr_to_upipe_mgr(ts_mux_mgr);
}

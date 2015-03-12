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
 * @short Upipe higher-level module muxing elementary streams in a TS
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
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/upipe_ts_psi_generator.h>
#include <upipe-ts/upipe_ts_tstd.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
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
/** max retention time for still pictures streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_STILL (UCLOCK_FREQ * 60)
/** max retention time for teletext (ETSI EN 300 472 5.) */
#define MAX_DELAY_TELX (UCLOCK_FREQ / 25)
/** fixed PES header size for teletext (ETSI EN 300 472 4.2) */
#define PES_HEADER_SIZE_TELX 45

/** default minimum duration of audio PES */
#define DEFAULT_AUDIO_PES_MIN_DURATION (UCLOCK_FREQ / 25)
/** max interval between PCRs (ISO/IEC 13818-1 2.7.2) */
#define MAX_PCR_INTERVAL (UCLOCK_FREQ / 10)
/** default interval between PCRs */
#define DEFAULT_PCR_INTERVAL (UCLOCK_FREQ / 15)
/** default interval between PATs and PMTs */
#define DEFAULT_PSI_INTERVAL_ISO (UCLOCK_FREQ / 4)
/** default interval between PATs and PMTs, in DVB conformance */
#define DEFAULT_PSI_INTERVAL_DVB (UCLOCK_FREQ / 10)
/** default interval between PATs and PMTs, in ATSC conformance */
#define DEFAULT_PSI_INTERVAL_ATSC (UCLOCK_FREQ / 10)
/** offset between the first PAT and the first PMT */
#define PAT_OFFSET (UCLOCK_FREQ / 100)
/** offset between the first PMT and the first ES packet */
#define PMT_OFFSET (UCLOCK_FREQ / 100)
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
    /** pointer to ts_psig manager */
    struct upipe_mgr *ts_psig_mgr;

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
    /** pointer to PAT ts_encaps */
    struct upipe *pat_encaps;
    /** probe passed to psig */
    struct uprobe pat_probe;
    /** PAT size */
    uint64_t pat_size;

    /** one TS packet of padding */
    struct ubuf *padding;

    /** current conformance */
    enum upipe_ts_conformance conformance;
    /** interval between PATs */
    uint64_t pat_interval;
    /** default interval between PMTs */
    uint64_t pmt_interval;
    /** default interval between PCRs */
    uint64_t pcr_interval;
    /** default maximum retention delay */
    uint64_t max_delay;
    /** muxing delay */
    uint64_t mux_delay;
    /** initial cr_prog */
    uint64_t initial_cr_prog;
    /** last attributed automatic SID */
    uint16_t sid_auto;
    /** last attributed automatic PID */
    uint16_t pid_auto;

    /** octetrate assigned by the application, or 0 */
    uint64_t fixed_octetrate;
    /** octetrate reserved for padding (and emergency situation) */
    uint64_t padding_octetrate;
    /** total octetrate including overheads, PMTs and PAT */
    uint64_t total_octetrate;
    /** interval between packets (rounded up, not to be used anywhere
     * critical */
    uint64_t interval;
    /** mux mode */
    enum upipe_ts_mux_mode mode;
    /** MTU */
    size_t mtu;
    /** size of the TB buffer */
    size_t tb_size;

    /** max latency of the subpipes */
    uint64_t latency;
    /** date of the current uref (system time, latency taken into account) */
    uint64_t cr_sys;
    /** remainder of the uref_size / octetrate calculation */
    uint64_t cr_sys_remainder;
    /** current aggregation */
    struct uref *uref;

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
    /** pointer to PMT ts_encaps */
    struct upipe *pmt_encaps;
    /** probe passed to psig_program */
    struct uprobe pmt_probe;
    /** PMT size */
    uint64_t pmt_size;

    /** total octetrate including overheads and PMT */
    uint64_t total_octetrate;

    /** interval between PMTs */
    uint64_t pmt_interval;
    /** interval between PCRs */
    uint64_t pcr_interval;
    /** maximum retention delay */
    uint64_t max_delay;

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

/** @internal @This defines the role of an input wrt. PCR management. */
enum upipe_ts_mux_input_type {
    /** video */
    UPIPE_TS_MUX_INPUT_VIDEO,
    /** audio */
    UPIPE_TS_MUX_INPUT_AUDIO,
    /** other (unsuitable for PCR) */
    UPIPE_TS_MUX_INPUT_OTHER
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
    /** total octetrate including overheads */
    uint64_t total_octetrate;

    /** proxy probe */
    struct uprobe probe;
    /** list of input bin requests */
    struct uchain input_request_list;
    /** pointer to ts_tstd */
    struct upipe *tstd;
    /** pointer to ts_encaps */
    struct upipe *encaps;

    /** maximum retention delay */
    uint64_t max_delay;
    /** number of access units per second */
    struct urational au_per_sec;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux_input, upipe, UPIPE_TS_MUX_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_mux_input, urefcount,
                       upipe_ts_mux_input_no_input)
UPIPE_HELPER_VOID(upipe_ts_mux_input)
UPIPE_HELPER_BIN_INPUT(upipe_ts_mux_input, tstd, input_request_list)

UBASE_FROM_TO(upipe_ts_mux_input, urefcount, urefcount_real, urefcount_real)

UPIPE_HELPER_SUBPIPE(upipe_ts_mux_program, upipe_ts_mux_input, input,
                     input_mgr, inputs, uchain)

/** @hidden */
static void upipe_ts_mux_input_free(struct urefcount *urefcount_real);


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
    upipe_ts_mux_input->pcr = false;
    upipe_ts_mux_input->deleted = false;
    upipe_ts_mux_input->input_type = UPIPE_TS_MUX_INPUT_OTHER;
    upipe_ts_mux_input->pid = 0;
    upipe_ts_mux_input->octetrate = 0;
    upipe_ts_mux_input->buffer_duration = 0;
    upipe_ts_mux_input->total_octetrate = 0;
    upipe_ts_mux_input->encaps = NULL;
    upipe_ts_mux_input->max_delay = program->max_delay;
    upipe_ts_mux_input->au_per_sec.num = upipe_ts_mux_input->au_per_sec.den = 0;

    upipe_ts_mux_input_init_sub(upipe);
    uprobe_init(&upipe_ts_mux_input->probe, upipe_ts_mux_input_probe, NULL);
    upipe_ts_mux_input->probe.refcount =
        upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input);
    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)->mgr);
    struct upipe *tstd, *psig_flow;
    if (unlikely((tstd =
                  upipe_void_alloc(ts_mux_mgr->ts_tstd_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "tstd"))) == NULL ||
                 (upipe_ts_mux_input->encaps =
                  upipe_void_alloc_output(tstd,
                         ts_mux_mgr->ts_encaps_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "encaps"))) == NULL ||
                 (psig_flow =
                  upipe_void_alloc_output_sub(upipe_ts_mux_input->encaps,
                         program->psig_program,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "psig flow"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    upipe_ts_mux_input_store_first_inner(upipe, tstd);
    upipe_ts_encaps_set_tb_size(upipe_ts_mux_input->encaps,
                                upipe_ts_mux->tb_size);
    upipe_set_opaque(upipe_ts_mux_input->encaps, upipe_ts_mux_input);
    upipe_set_output(psig_flow,
                     upipe_ts_mux_to_inner_sink(upipe_ts_mux));
    upipe_release(psig_flow);
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

    upipe_ts_mux_input_bin_input(upipe, uref, upump_p);
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

    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(upipe->mgr);
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_program_mgr(
                upipe_ts_mux_program_to_upipe(program)->mgr);
    const char *def;
    uint64_t octetrate;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (!ubase_check(uref_block_flow_get_octetrate(flow_def, &octetrate)) ||
        !octetrate) {
        UBASE_RETURN(uref_block_flow_get_max_octetrate(flow_def, &octetrate));
        upipe_warn_va(upipe, "using max octetrate %"PRIu64" bits/s",
                      octetrate * 8);
    }
    if (ubase_ncmp(def, "block.") || !octetrate)
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    uint64_t pes_overhead = 0;
    enum upipe_ts_mux_input_type input_type = UPIPE_TS_MUX_INPUT_OTHER;
    uint64_t buffer_size = 0;
    uint64_t max_delay = MAX_DELAY;
    struct urational au_per_sec;
    au_per_sec.num = au_per_sec.den = 0;
    bool au_irregular = true;
    bool pes_alignment = false;

    if (strstr(def, ".pic.sub.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_OTHER;
        if (!ubase_ncmp(def, "block.dvb_teletext.")) {
            buffer_size = BS_TELX;
            max_delay = MAX_DELAY_TELX;
            au_irregular = false;
            pes_alignment = true;
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_PRIVATE_PES))
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_header(flow_def_dup,
                                                PES_HEADER_SIZE_TELX));
            UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup,
                                                TB_RATE_TELX));

            uint8_t languages = 0;
            uref_flow_get_languages(flow_def, &languages);
            uint8_t telx_descriptor[DESC56_HEADER_SIZE +
                                    DESC56_LANGUAGE_SIZE * languages];
            desc56_init(telx_descriptor);
            desc_set_length(telx_descriptor,
                            DESC56_HEADER_SIZE +
                            DESC56_LANGUAGE_SIZE * languages -
                            DESC_HEADER_SIZE);
            for (uint8_t j = 0; j < languages; j++) {
                uint8_t *language = desc56_get_language(telx_descriptor, j);
                const char *lang = "unk";
                uref_flow_get_language(flow_def, &lang, j);
                if (strlen(lang) < 3)
                    lang = "unk";
                desc56n_set_code(language, (const uint8_t *)lang);
                uint8_t telx_type = DESC56_TELETEXTTYPE_INFORMATION;
                uref_ts_flow_get_telx_type(flow_def, &telx_type, j);
                desc56n_set_teletexttype(language, telx_type);
                uint8_t telx_magazine = 0;
                uref_ts_flow_get_telx_magazine(flow_def, &telx_magazine, j);
                desc56n_set_teletextmagazine(language, telx_magazine);
                uint8_t telx_page = 0;
                uref_ts_flow_get_telx_page(flow_def, &telx_page, j);
                desc56n_set_teletextpage(language, telx_page);
            }
            UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def_dup,
                    telx_descriptor,
                    DESC56_HEADER_SIZE + DESC56_LANGUAGE_SIZE * languages));

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
            uref_block_flow_get_buffer_size(flow_def, &buffer_size);
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_PRIVATE_PES))
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));
            UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup,
                                                octetrate));

            uint8_t languages = 0;
            uref_flow_get_languages(flow_def, &languages);
            uint8_t dvbsub_descriptor[DESC59_HEADER_SIZE +
                                      DESC59_LANGUAGE_SIZE * languages];
            desc59_init(dvbsub_descriptor);
            desc_set_length(dvbsub_descriptor,
                            DESC59_HEADER_SIZE +
                            DESC59_LANGUAGE_SIZE * languages -
                            DESC_HEADER_SIZE);
            for (uint8_t j = 0; j < languages; j++) {
                uint8_t *language = desc59_get_language(dvbsub_descriptor, j);
                const char *lang = "unk";
                uref_flow_get_language(flow_def, &lang, j);
                if (strlen(lang) < 3)
                    lang = "unk";
                desc59n_set_code(language, (const uint8_t *)lang);
                /* DVB-subtitles (normal) with no AR criticality */
                uint8_t dvbsub_type = 0x10;
                uref_ts_flow_get_sub_type(flow_def, &dvbsub_type, j);
                desc59n_set_subtitlingtype(language, dvbsub_type);
                uint8_t dvbsub_composition = 0;
                uref_ts_flow_get_sub_composition(flow_def, &dvbsub_composition, j);
                desc59n_set_compositionpage(language, dvbsub_composition);
                uint8_t dvbsub_ancillary = 0;
                uref_ts_flow_get_sub_ancillary(flow_def, &dvbsub_ancillary, j);
                desc59n_set_ancillarypage(language, dvbsub_ancillary);
            }
            UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def_dup,
                    dvbsub_descriptor,
                    DESC59_HEADER_SIZE + DESC59_LANGUAGE_SIZE * languages));

            /* PES header overhead - worst case one subtitle by frame in a
             * 30 Hz system */
            au_per_sec.num = 30;
            au_per_sec.den = 1;
            pes_overhead += PES_HEADER_SIZE_PTS *
                (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
        }

    } else if (strstr(def, ".pic.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_VIDEO;
        if (!ubase_ncmp(def, "block.mpeg1video.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_VIDEO_MPEG1))
            pes_alignment = true;
        } else if (!ubase_ncmp(def, "block.mpeg2video.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_VIDEO_MPEG2));
            pes_alignment = true;
        } else if (!ubase_ncmp(def, "block.mpeg4.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_VIDEO_MPEG4));
            max_delay = MAX_DELAY_14496;
        } else if (!ubase_ncmp(def, "block.h264.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_VIDEO_AVC));
            max_delay = MAX_DELAY_14496;
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

        if (!ubase_ncmp(def, "block.mp2.") || !ubase_ncmp(def, "block.mp3.")) {
            pes_alignment = true;
            buffer_size = BS_ADTS_2;
            uint64_t rate;
            if (ubase_check(uref_sound_flow_get_rate(flow_def, &rate)) &&
                rate >= 32000) {
                UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                   PMT_STREAMTYPE_AUDIO_MPEG1));
            } else {
                UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                   PMT_STREAMTYPE_AUDIO_MPEG2));
            }
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                 PES_STREAM_ID_AUDIO_MPEG));
        } else if (!ubase_ncmp(def, "block.aac.")) {
            uint8_t channels = 2;
            uref_sound_flow_get_channels(flow_def_dup, &channels);
            if (channels <= 2)
                buffer_size = BS_ADTS_2;
            else if (channels <= 8)
                buffer_size = BS_ADTS_8;
            else if (channels <= 12)
                buffer_size = BS_ADTS_12;
            else
                buffer_size = BS_ADTS_48;

            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_AUDIO_ADTS));
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_AUDIO_MPEG));
        } else if (!ubase_ncmp(def, "block.ac3.")) {
            buffer_size = BS_A52;
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                               PMT_STREAMTYPE_PRIVATE_PES));
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                 PES_STREAM_ID_PRIVATE_1));

            uint8_t ac3_descriptor[DESC6A_HEADER_SIZE];
            desc6a_init(ac3_descriptor);
            desc_set_length(ac3_descriptor,
                            DESC6A_HEADER_SIZE - DESC_HEADER_SIZE);
            desc6a_clear_flags(ac3_descriptor);
            UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def_dup,
                    ac3_descriptor, DESC6A_HEADER_SIZE));
            pes_min_duration = 0;
        } else if (!ubase_ncmp(def, "block.eac3.")) {
            buffer_size = BS_A52;
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_PRIVATE_PES));
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_id(flow_def_dup,
                                                PES_STREAM_ID_PRIVATE_1));

            uint8_t eac3_descriptor[DESC7A_HEADER_SIZE];
            desc7a_init(eac3_descriptor);
            desc_set_length(eac3_descriptor,
                            DESC7A_HEADER_SIZE - DESC_HEADER_SIZE);
            desc7a_clear_flags(eac3_descriptor);
            UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def_dup,
                    eac3_descriptor, DESC7A_HEADER_SIZE));
            pes_min_duration = 0;
        } else {
            buffer_size = BS_ADTS_2;
        }

        UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup, TB_RATE_AUDIO));
        if (!ubase_check(uref_ts_flow_get_pes_min_duration(flow_def_dup, &pes_min_duration)))
            UBASE_FATAL(upipe, uref_ts_flow_set_pes_min_duration(flow_def_dup,
                                                           pes_min_duration));

        uint8_t languages;
        if (ubase_check(uref_flow_get_languages(flow_def, &languages)) &&
            languages) {
            uint8_t lang_descriptor[DESC0A_HEADER_SIZE +
                                    DESC0A_LANGUAGE_SIZE * languages];
            desc0a_init(lang_descriptor);
            desc_set_length(lang_descriptor,
                            DESC0A_HEADER_SIZE +
                            DESC0A_LANGUAGE_SIZE * languages -
                            DESC_HEADER_SIZE);
            for (uint8_t j = 0; j < languages; j++) {
                uint8_t *language = desc0a_get_language(lang_descriptor, j);
                const char *lang = "unk";
                uref_flow_get_language(flow_def, &lang, j);
                if (strlen(lang) < 3)
                    lang = "unk";
                desc0an_set_code(language, (const uint8_t *)lang);
                if (ubase_check(uref_flow_get_hearing_impaired(flow_def, j)))
                    desc0an_set_audiotype(lang_descriptor + DESC0A_HEADER_SIZE,
                                          DESC0A_TYPE_HEARING_IMP);
                else if (ubase_check(uref_flow_get_visual_impaired(flow_def, j)))
                    desc0an_set_audiotype(lang_descriptor + DESC0A_HEADER_SIZE,
                                          DESC0A_TYPE_VISUAL_IMP);
                else
                    desc0an_set_audiotype(lang_descriptor + DESC0A_HEADER_SIZE,
                                          DESC0A_TYPE_UNDEFINED);
            }
            UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def_dup,
                    lang_descriptor,
                    DESC0A_HEADER_SIZE + DESC0A_LANGUAGE_SIZE * languages));
        }

        uint64_t rate = 48000, samples = 1152;
        uref_sound_flow_get_rate(flow_def, &rate);
        uref_sound_flow_get_samples(flow_def, &samples);
        unsigned int nb_frames = 1;
        while (samples * nb_frames * UCLOCK_FREQ / rate < pes_min_duration)
            nb_frames++;
        samples *= nb_frames;

        au_per_sec.num = rate;
        au_per_sec.den = samples;
        urational_simplify(&au_per_sec);

        /* PES header overhead */
        pes_overhead += PES_HEADER_SIZE_PTS *
            (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
    }

    if (au_irregular && pes_alignment) {
        /* TS padding overhead */
        pes_overhead += (TS_SIZE - TS_HEADER_SIZE) *
            (au_per_sec.num + au_per_sec.den - 1) / au_per_sec.den;
    }

    if (buffer_size > max_delay * octetrate / UCLOCK_FREQ) {
        buffer_size = max_delay * octetrate / UCLOCK_FREQ;
        upipe_warn_va(upipe, "lowering buffer size to %"PRIu64" octets to comply with T-STD max retention", buffer_size);
    }
    if (buffer_size > upipe_ts_mux_input->max_delay * octetrate / UCLOCK_FREQ) {
        buffer_size = upipe_ts_mux_input->max_delay * octetrate / UCLOCK_FREQ;
        upipe_warn_va(upipe, "lowering buffer size to %"PRIu64" octets to comply with configured max retention", buffer_size);
    }
    UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def_dup, octetrate));
    UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def_dup,
                                                       buffer_size));
    upipe_ts_mux_input->buffer_duration = UCLOCK_FREQ * buffer_size / octetrate;

    if (pes_alignment) {
        UBASE_FATAL(upipe, uref_ts_flow_set_pes_alignment(flow_def_dup))
    }
    upipe_ts_mux_input->au_per_sec = au_per_sec;

    uint64_t ts_overhead = TS_HEADER_SIZE *
        (octetrate + pes_overhead + TS_SIZE - TS_HEADER_SIZE - 1) /
        (TS_SIZE - TS_HEADER_SIZE);

    uint64_t pid = upipe_ts_mux_input->pid;
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

    if (!ubase_check(upipe_ts_mux_set_max_delay(upipe_ts_mux_input->tstd,
                                        upipe_ts_mux_input->max_delay)) ||
        !ubase_check(upipe_set_flow_def(upipe_ts_mux_input->tstd,
                                        flow_def_dup))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_ALLOC;
    }
    uref_free(flow_def_dup);

    upipe_ts_mux_input->input_type = input_type;
    upipe_ts_mux_input->pid = pid;
    upipe_ts_mux_input->octetrate = octetrate;
    upipe_ts_mux_input->total_octetrate = octetrate +
                                          pes_overhead + ts_overhead;

    uint64_t latency = 0;
    uref_clock_get_latency(flow_def, &latency);
    /* we never lower latency */
    if (latency + upipe_ts_mux_input->buffer_duration > upipe_ts_mux->latency) {
        upipe_ts_mux->latency = latency + upipe_ts_mux_input->buffer_duration;
        upipe_ts_mux_build_flow_def(upipe_ts_mux_to_upipe(upipe_ts_mux));
    } else
        upipe_set_max_length(upipe_ts_mux_input->encaps,
                (MIN_BUFFERING + upipe_ts_mux->latency) * au_per_sec.num /
                au_per_sec.den / UCLOCK_FREQ);

    upipe_notice_va(upipe,
            "adding %s on PID %"PRIu64" (%"PRIu64" bits/s), latency %"PRIu64" ms",
            def, pid, octetrate * 8, latency * 1000 / UCLOCK_FREQ);
    upipe_ts_mux_program_change(upipe_ts_mux_program_to_upipe(program));
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
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_input_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_input_get_super(upipe, p);
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

        case UPIPE_TS_MUX_GET_MAX_DELAY:
        case UPIPE_TS_MUX_SET_MAX_DELAY: {
            struct upipe_ts_mux_input *upipe_ts_mux_input =
                upipe_ts_mux_input_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux_input->tstd, command, args);
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
    upipe_ts_mux_input_clean_bin_input(upipe);
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

/** @internal @This catches the events from psig_program inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_program_pmt_probe(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        container_of(uprobe, struct upipe_ts_mux_program, pmt_probe);
    struct upipe *upipe = upipe_ts_mux_program_to_upipe(upipe_ts_mux_program);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    if (event != UPROBE_NEW_FLOW_DEF)
        return upipe_throw_proxy(upipe, inner, event, args);

    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t pmt_size = TS_SIZE - TS_HEADER_SIZE;
    uref_block_flow_get_size(flow_def, &pmt_size);
    /* pointer_field overhead */
    pmt_size++;
    /* TS header overhead */
    pmt_size = TS_SIZE * (pmt_size * TS_HEADER_SIZE - 1) / TS_HEADER_SIZE;
    if (pmt_size != upipe_ts_mux_program->pmt_size) {
        upipe_ts_mux_program->pmt_size = pmt_size;
        upipe_ts_mux_program_update(upipe);
    }
    return UBASE_ERR_NONE;
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
    upipe_ts_mux_program->psig_program = NULL;
    upipe_ts_mux_program->pmt_size = TS_SIZE;
    upipe_ts_mux_program->sid = 0;
    upipe_ts_mux_program->pmt_pid = 8192;
    upipe_ts_mux_program->pmt_interval = upipe_ts_mux->pmt_interval;
    upipe_ts_mux_program->pcr_interval = upipe_ts_mux->pcr_interval;
    upipe_ts_mux_program->max_delay = upipe_ts_mux->max_delay;
    upipe_ts_mux_program->total_octetrate = 0;
    upipe_ts_mux_program_init_sub(upipe);

    uprobe_init(&upipe_ts_mux_program->probe, upipe_ts_mux_program_probe, NULL);
    upipe_ts_mux_program->probe.refcount =
        upipe_ts_mux_program_to_urefcount_real(upipe_ts_mux_program);
    uprobe_init(&upipe_ts_mux_program->pmt_probe,
                upipe_ts_mux_program_pmt_probe, NULL);
    upipe_ts_mux_program->pmt_probe.refcount =
        upipe_ts_mux_program_to_urefcount_real(upipe_ts_mux_program);

    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)->mgr);

    struct upipe *psig_program;
    if (unlikely((psig_program =
                  upipe_void_alloc_sub(upipe_ts_mux->psig,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_mux_program->pmt_probe),
                             UPROBE_LOG_VERBOSE, "psig program"))) == NULL ||
                 (upipe_ts_mux_program->pmt_encaps =
                  upipe_void_alloc_output(psig_program,
                         ts_mux_mgr->ts_encaps_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_mux_program->probe),
                             UPROBE_LOG_VERBOSE, "pmt encaps"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }
    upipe_ts_mux_program_store_first_inner(upipe, psig_program);
    upipe_ts_encaps_set_tb_size(upipe_ts_mux_program->pmt_encaps,
                                upipe_ts_mux->tb_size);
    upipe_set_output(upipe_ts_mux_program->pmt_encaps,
                     upipe_ts_mux_to_inner_sink(upipe_ts_mux));
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
    uint64_t total_octetrate = 0;

    if (upipe_ts_mux_program->pmt_interval)
        total_octetrate += upipe_ts_mux_program->pmt_size *
            ((UCLOCK_FREQ + upipe_ts_mux_program->pmt_interval - 1) /
               upipe_ts_mux_program->pmt_interval);
    if (upipe_ts_mux_program->pcr_interval)
        total_octetrate += (uint64_t)TS_SIZE *
             ((UCLOCK_FREQ + upipe_ts_mux_program->pcr_interval - 1) /
              upipe_ts_mux_program->pcr_interval);

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        total_octetrate += input->total_octetrate;
    }

    if (total_octetrate != upipe_ts_mux_program->total_octetrate) {
        upipe_ts_mux_program->total_octetrate = total_octetrate;

        struct upipe_ts_mux *upipe_ts_mux =
            upipe_ts_mux_from_program_mgr(upipe->mgr);
        upipe_ts_mux_update(upipe_ts_mux_to_upipe(upipe_ts_mux));
    }
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
        if (input->input_type == UPIPE_TS_MUX_INPUT_OTHER)
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

/** @internal @This sets the initial cr_prog of the program to 0.
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
        uint64_t cr_sys;
        int err = upipe_ts_encaps_peek(input->encaps, &cr_sys);
        ubase_assert(err);
        if (cr_sys < min_cr_sys)
            min_cr_sys = cr_sys;
    }

    ulist_foreach (&program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        uint64_t cr_sys;
        int err = upipe_ts_encaps_peek(input->encaps, &cr_sys);
        ubase_assert(err);
        upipe_ts_mux_set_cr_prog(input->encaps, cr_prog + cr_sys - min_cr_sys);
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
    if (ubase_check(uref_ts_flow_get_pid(flow_def, &pid)) &&
        upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(mux), pid)) {
        upipe_warn_va(upipe_ts_mux_to_upipe(mux),
                      "PID %"PRIu64" already exists", pid);
        pid = 0;
    }
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

    uint64_t octetrate;
    if (!ubase_check(uref_block_flow_get_octetrate(flow_def, &octetrate)))
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def_dup, TB_RATE_PSI))
    UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup, TB_RATE_PSI))

    if (!ubase_check(upipe_set_flow_def(program->psig_program, flow_def_dup))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_INVALID;
    }

    uref_free(flow_def_dup);
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
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_program_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_mux_program_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_program_iterate_sub(upipe, p);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_program_get_super(upipe, p);
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
            return upipe_control_va(upipe_ts_mux_program->pmt_encaps,
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
    uprobe_clean(&upipe_ts_mux_program->pmt_probe);
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

    upipe_ts_mux_program_clean_bin_input(upipe);
    upipe_release(upipe_ts_mux_program->pmt_encaps);
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
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_mux_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_mux_free_output_proxy(upipe, request);
        }
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
    sink->refcount = upipe_ts_mux_to_urefcount(upipe_ts_mux);
    upipe_init(sink, inner_sink_mgr,
               uprobe_pfx_alloc(uprobe_use(upipe->uprobe), UPROBE_LOG_VERBOSE,
                                "inner sink"));
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

/** @internal @This catches the events from psig inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_mux_pat_probe(struct uprobe *uprobe, struct upipe *inner,
                                  int event, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux =
        container_of(uprobe, struct upipe_ts_mux, pat_probe);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    if (event == UPROBE_NEED_OUTPUT)
        return UBASE_ERR_UNHANDLED;
    if (event != UPROBE_NEW_FLOW_DEF)
        return upipe_throw_proxy(upipe, inner, event, args);

    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t pat_size = PSI_HEADER_SIZE;
    uref_block_flow_get_size(flow_def, &pat_size);
    /* pointer_field overhead */
    pat_size += (pat_size + PSI_MAX_SIZE + PSI_HEADER_SIZE - 1) /
                (PSI_MAX_SIZE + PSI_HEADER_SIZE);
    /* TS header overhead */
    pat_size = TS_SIZE * (pat_size * TS_HEADER_SIZE - 1) / TS_HEADER_SIZE;
    if (pat_size != upipe_ts_mux->pat_size) {
        upipe_ts_mux->pat_size = pat_size;
        upipe_ts_mux_update(upipe);
    }
    return UBASE_ERR_NONE;
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
    upipe_ts_mux->pat_encaps = NULL;
    upipe_ts_mux->pat_size = TS_SIZE;
    upipe_ts_mux->padding = NULL;
    upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_ISO;
    upipe_ts_mux->pat_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pmt_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pcr_interval = DEFAULT_PCR_INTERVAL;
    upipe_ts_mux->max_delay = UINT64_MAX;
    upipe_ts_mux->mux_delay = DEFAULT_MUX_DELAY;
    upipe_ts_mux->initial_cr_prog = UINT64_MAX;
    upipe_ts_mux->sid_auto = DEFAULT_SID_AUTO;
    upipe_ts_mux->pid_auto = DEFAULT_PID_AUTO;
    upipe_ts_mux->fixed_octetrate = 0;
    upipe_ts_mux->padding_octetrate = 0;
    upipe_ts_mux->total_octetrate = 0;
    upipe_ts_mux->interval = 0;
    upipe_ts_mux->mode = UPIPE_TS_MUX_MODE_CBR;
    upipe_ts_mux->tb_size = T_STD_TS_BUFFER;
    upipe_ts_mux->mtu = TS_SIZE;
    upipe_ts_mux->latency = 0;
    upipe_ts_mux->cr_sys = UINT64_MAX;
    upipe_ts_mux->cr_sys_remainder = 0;
    upipe_ts_mux->uref = NULL;

    uprobe_init(&upipe_ts_mux->probe, upipe_ts_mux_probe, NULL);
    upipe_ts_mux->probe.refcount = upipe_ts_mux_to_urefcount_real(upipe_ts_mux);
    uprobe_init(&upipe_ts_mux->pat_probe, upipe_ts_mux_pat_probe, NULL);
    upipe_ts_mux->pat_probe.refcount =
        upipe_ts_mux_to_urefcount_real(upipe_ts_mux);

    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe *psig;
    if (unlikely((psig = upipe_void_alloc(ts_mux_mgr->ts_psig_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_mux->pat_probe),
                             UPROBE_LOG_VERBOSE, "psig"))) == NULL ||
                 (upipe_ts_mux->pat_encaps =
                  upipe_void_alloc_output(psig,
                         ts_mux_mgr->ts_encaps_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_mux->probe),
                             UPROBE_LOG_VERBOSE, "pat encaps"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }
    upipe_ts_mux_store_first_inner(upipe, psig);
    upipe_ts_encaps_set_tb_size(upipe_ts_mux->pat_encaps,
                                upipe_ts_mux->tb_size);
    upipe_set_output(upipe_ts_mux->pat_encaps,
                     upipe_ts_mux_to_inner_sink(upipe_ts_mux));
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
    uint64_t original_cr_sys = mux->cr_sys - mux->latency;
    upipe_ts_psig_prepare(mux->psig, original_cr_sys);
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

/** @internal @This checks if a ts_encaps has a packet to output, and if its
 * date is earlier than the date of reference. In that case, cr_sys_p and
 * encaps_p are updated.
 *
 * Be careful that this function may release inputs under pending deletion,
 * so the caller must protect itself.
 *
 * @param encaps description structure of the ts_encaps pipe
 * @param next_cr_sys data prior to that will be deleted
 * @param cr_sys_p date of reference
 * @param encaps_p ts_encaps of reference
 * @return true in case of emergency muxing
 */
static bool upipe_ts_mux_prepare(struct upipe *upipe, struct upipe *encaps,
                                 uint64_t next_cr_sys, uint64_t *cr_sys_p,
                                 struct upipe **encaps_p)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    uint64_t cr_sys, dts_sys;
    if (!ubase_check(upipe_ts_encaps_prepare(encaps, next_cr_sys, &cr_sys,
                                             &dts_sys)))
        return false;
    if (dts_sys <= next_cr_sys + mux->interval) {
        *encaps_p = encaps;
        return true;
    }
    if (cr_sys >= *cr_sys_p)
        return false;
    *cr_sys_p = cr_sys;
    *encaps_p = encaps;
    return false;
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
    struct uchain *uchain_program;
    uint64_t cr_sys = UINT64_MAX;
    struct upipe *encaps;
    *ubuf_p = NULL;

    /* Order of priority: 1. PSI */
    if (upipe_ts_mux_prepare(upipe, mux->pat_encaps, original_cr_sys, &cr_sys,
                             &encaps) || cr_sys <= original_cr_sys)
        goto upipe_ts_mux_splice_done;
    ulist_foreach (&mux->programs, uchain_program) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        if (upipe_ts_mux_prepare(upipe, program->pmt_encaps, original_cr_sys,
                                 &cr_sys, &encaps) || cr_sys <= original_cr_sys)
            goto upipe_ts_mux_splice_done;
    }

    /* 2. Inputs */
    ulist_foreach (&mux->programs, uchain_program) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        struct uchain *uchain_input;
        ulist_foreach (&program->inputs, uchain_input) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            if (upipe_ts_mux_prepare(upipe, input->encaps, original_cr_sys,
                                     &cr_sys, &encaps))
                goto upipe_ts_mux_splice_done;
        }
    }

    if (cr_sys - mux->interval > original_cr_sys)
        return;

upipe_ts_mux_splice_done:
    upipe_ts_encaps_splice(encaps, ubuf_p, dts_sys_p);

    struct upipe_ts_mux_input *input =
        upipe_get_opaque(encaps, struct upipe_ts_mux_input *);
    if (input != NULL && input->deleted &&
        !ubase_check(upipe_ts_encaps_peek(encaps, &cr_sys))) {
        /* This triggers the immediate deletion of the input. */
        upipe_release(encaps);
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
    upipe_ts_mux_output(upipe, uref, upump_p);
}

/** @internal @This runs when the pump expires (live mode only).
 *
 * @param upipe description structure of the pipe
 */
static void _upipe_ts_mux_watcher(struct upipe *upipe)
{
    upipe_use(upipe);
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    if (unlikely(mux->cr_sys == UINT64_MAX))
        mux->cr_sys = uclock_now(mux->uclock);

    upipe_ts_mux_increment(upipe);
    if (mux->uref != NULL) /* capped VBR */
        uref_clock_set_cr_sys(mux->uref, mux->cr_sys - mux->latency);

    size_t uref_size;
    while (mux->uref == NULL ||
           (ubase_check(uref_block_size(mux->uref, &uref_size)) &&
            uref_size < mux->mtu)) {
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
        while (mux->uref == NULL ||
               (ubase_check(uref_block_size(mux->uref, &uref_size)) &&
                uref_size < mux->mtu)) {
            struct ubuf *ubuf = ubuf_dup(mux->padding);
            if (ubuf == NULL)
                break;
            upipe_ts_mux_append(upipe, ubuf, UINT64_MAX);
        }
    }

    if (mux->uref != NULL &&
        ubase_check(uref_block_size(mux->uref, &uref_size)) &&
        uref_size >= mux->mtu)
        upipe_ts_mux_complete(upipe, &mux->upump);

    /* Check for deleted inputs */
    struct uchain *uchain_program, *uchain_program_tmp;
    ulist_delete_foreach (&mux->programs, uchain_program, uchain_program_tmp) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        upipe_use(upipe_ts_mux_program_to_upipe(program));

        struct uchain *uchain_input, *uchain_input_tmp;
        ulist_delete_foreach (&program->inputs, uchain_input,
                              uchain_input_tmp) {
            struct upipe_ts_mux_input *input =
                upipe_ts_mux_input_from_uchain(uchain_input);
            uint64_t cr_sys;
            if (input->deleted &&
                !ubase_check(upipe_ts_encaps_peek(input->encaps, &cr_sys)))
                upipe_release(input->encaps);
        }
        upipe_release(upipe_ts_mux_program_to_upipe(program));
    }

    upipe_ts_mux_set_upump(upipe, NULL);
    upipe_ts_mux_work(upipe, NULL);
    upipe_release(upipe);
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
            uint64_t cr_sys;
            if (!ubase_check(upipe_ts_encaps_peek(input->encaps, &cr_sys))) {
                if (input->deleted) {
                    upipe_release(input->encaps);
                    continue;
                } else {
                    upipe_release(upipe_ts_mux_program_to_upipe(program));
                    return UINT64_MAX;
                }
            }
            if (min_cr_sys > cr_sys)
                min_cr_sys = cr_sys;
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
            upipe_ts_psig_prepare(mux->psig, min_cr_sys);
        }

        struct ubuf *ubuf;
        uint64_t dts_sys;
        size_t uref_size;
        upipe_ts_mux_splice(upipe, &ubuf, &dts_sys);
        if (ubuf != NULL) {
            upipe_ts_mux_append(upipe, ubuf, dts_sys);
            if (ubase_check(uref_block_size(mux->uref, &uref_size)) &&
                uref_size >= mux->mtu) {
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

        while (mux->uref == NULL ||
               (ubase_check(uref_block_size(mux->uref, &uref_size)) &&
                uref_size < mux->mtu)) {
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
        if (next_cr_sys > now) {
            upump = upump_alloc_timer(mux->upump_mgr, upipe_ts_mux_watcher,
                                      upipe, next_cr_sys - now, 0);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return;
            }
        } else if (next_cr_sys > now - mux->mux_delay) {
            _upipe_ts_mux_watcher(upipe);
            return;
        } else
            upipe_warn_va(upipe, "missed a tick by %"PRIu64" ms",
                          (now - next_cr_sys) * 1000 / UCLOCK_FREQ);
    }
    if (unlikely(upump == NULL)) {
        mux->cr_sys = UINT64_MAX;
        mux->cr_sys_remainder = 0;
        upump = upump_alloc_idler(mux->upump_mgr, upipe_ts_mux_watcher, upipe);
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
    if (unlikely(mux->flow_def == NULL || mux->padding == NULL))
        return;

    if (urequest_get_opaque(&mux->uclock_request, struct upipe *) == NULL)
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
    upipe_notice_va(upipe,
            "now operating in %s mode at %"PRIu64" bits/s (conformance %s) with end-to-end latency %"PRIu64" ms",
            upipe_ts_mux_mode_print(mux->mode), mux->total_octetrate * 8,
            upipe_ts_conformance_print(mux->conformance),
            (mux->latency + mux->mux_delay) * 1000 / UCLOCK_FREQ);
}

/** @This calculates the total octetrate used by a stream and updates the
 * mux.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_update(struct upipe *upipe)
{
    struct upipe_ts_mux *mux = upipe_ts_mux_from_upipe(upipe);
    uint64_t total_octetrate = mux->fixed_octetrate;
    if (!total_octetrate) {
        total_octetrate = mux->padding_octetrate;
        if (mux->pat_interval)
            total_octetrate += mux->pat_size *
                ((UCLOCK_FREQ + mux->pat_interval - 1) / mux->pat_interval);

        struct uchain *uchain;
        ulist_foreach (&mux->programs, uchain) {
            struct upipe_ts_mux_program *program =
                upipe_ts_mux_program_from_uchain(uchain);
            total_octetrate += program->total_octetrate;
        }

        /* Add a margin to take into account 1/ the drift of the input clock
         * 2/ the drift of the output clock. */
        total_octetrate += (total_octetrate * PCR_TOLERANCE_PPM * 2 + 999999) /
                           1000000;
    }

    if (total_octetrate != mux->total_octetrate) {
        mux->total_octetrate = total_octetrate;
        upipe_ts_mux_build_flow_def(upipe);
        upipe_ts_mux_set_upump(upipe, NULL);
    }

    if (mux->total_octetrate) {
        mux->interval = (mux->mtu * UCLOCK_FREQ + mux->total_octetrate - 1) /
                        mux->total_octetrate;
        upipe_ts_mux_set_pat_interval(mux->psig,
                mux->pat_interval - mux->interval);

        struct uchain *uchain_program;
        ulist_foreach (&mux->programs, uchain_program) {
            struct upipe_ts_mux_program *program =
                upipe_ts_mux_program_from_uchain(uchain_program);
            upipe_ts_mux_set_pmt_interval(program->psig_program,
                    program->pmt_interval - mux->interval);

            struct uchain *uchain_input;
            ulist_foreach (&program->inputs, uchain_input) {
                struct upipe_ts_mux_input *input =
                    upipe_ts_mux_input_from_uchain(uchain_input);
                if (input->pcr && input->encaps != NULL)
                    upipe_ts_mux_set_pcr_interval(input->encaps,
                            program->pcr_interval - mux->interval);
            }
        }
    }

    upipe_ts_mux_notice(upipe);
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
            if (input->encaps != NULL && input->au_per_sec.den)
                upipe_set_max_length(input->encaps,
                        (MIN_BUFFERING + mux->latency) * input->au_per_sec.num /
                        input->au_per_sec.den / UCLOCK_FREQ);
        }
    }
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

    uint64_t tsid = 0;
    if (!ubase_check(uref_flow_get_id(flow_def, &tsid)))
        UBASE_FATAL(upipe, uref_flow_set_id(flow_def_dup, DEFAULT_TSID));

    uint64_t octetrate;
    if (!ubase_check(uref_block_flow_get_octetrate(flow_def, &octetrate)))
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def_dup, TB_RATE_PSI));
    UBASE_FATAL(upipe, uref_ts_flow_set_tb_rate(flow_def_dup, TB_RATE_PSI));
    UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def_dup, 0));

    int err = upipe_set_flow_def(upipe_ts_mux->psig, flow_def_dup);
    uref_free(flow_def_dup);
    return err;
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
    upipe_ts_mux->interval = (upipe_ts_mux->mtu * UCLOCK_FREQ +
                              upipe_ts_mux->total_octetrate - 1) /
                             upipe_ts_mux->total_octetrate;

    upipe_ts_mux->tb_size = T_STD_TS_BUFFER + mtu - TS_SIZE;
    upipe_ts_encaps_set_tb_size(upipe_ts_mux->pat_encaps,
                                upipe_ts_mux->tb_size);

    struct uchain *uchain_program;
    ulist_foreach (&upipe_ts_mux->programs, uchain_program) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain_program);
        upipe_ts_encaps_set_tb_size(program->pmt_encaps,
                                    upipe_ts_mux->tb_size);

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
    uint64_t max_psi_interval = UINT64_MAX;
    switch (conformance) {
        case UPIPE_TS_CONFORMANCE_AUTO:
        case UPIPE_TS_CONFORMANCE_ISO:
            upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_ISO;
            break;
        case UPIPE_TS_CONFORMANCE_DVB:
            upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_DVB;
            max_psi_interval = DEFAULT_PSI_INTERVAL_DVB;
            break;
        case UPIPE_TS_CONFORMANCE_ATSC:
            upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_ATSC;
            max_psi_interval = DEFAULT_PSI_INTERVAL_ATSC;
            break;
        default:
            return UBASE_ERR_INVALID;
    }

    if (upipe_ts_mux->pat_interval > max_psi_interval)
        upipe_ts_mux_set_pat_interval(upipe,
                max_psi_interval - upipe_ts_mux->interval);
    if (upipe_ts_mux->pmt_interval > max_psi_interval)
        upipe_ts_mux->pmt_interval = max_psi_interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->pmt_interval > max_psi_interval)
            upipe_ts_mux_set_pmt_interval(
                    upipe_ts_mux_program_to_upipe(program), max_psi_interval);
    }

    upipe_ts_mux_notice(upipe);
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

/** @internal @This sets the PCR octetrate.
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

/** @internal @This processes control commands on a ts_mux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_ts_mux_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_ts_mux_set_upump(upipe, NULL);
            return upipe_ts_mux_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_ts_mux_set_upump(upipe, NULL);
            upipe_ts_mux_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_mux_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_mux_set_output(upipe, output);
        }
        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *mtu_p = va_arg(args, unsigned int *);
            return upipe_ts_mux_get_output_size(upipe, mtu_p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int mtu = va_arg(args, unsigned int);
            return upipe_ts_mux_set_output_size(upipe, mtu);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_mux_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_iterate_sub(upipe, p);
        }

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

        case UPIPE_TS_MUX_GET_VERSION:
        case UPIPE_TS_MUX_SET_VERSION:
        case UPIPE_TS_MUX_FREEZE_PSI: {
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux->psig, command, args);
        }

        case UPIPE_TS_MUX_GET_CC:
        case UPIPE_TS_MUX_SET_CC: {
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
            return upipe_control_va(upipe_ts_mux->pat_encaps, command, args);
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
    uprobe_clean(&mux->probe);
    uprobe_clean(&mux->pat_probe);
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
    upipe_release(upipe_ts_mux->pat_encaps);

    upipe_ts_mux_clean_sub_programs(upipe);
    upipe_ts_mux_clean_bin_input(upipe);
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
    if (ts_mux_mgr->ts_tstd_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_tstd_mgr);
    if (ts_mux_mgr->ts_psig_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_psig_mgr);

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
        GET_SET_MGR(ts_psig, TS_PSIG)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
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

    ts_mux_mgr->ts_encaps_mgr = upipe_ts_encaps_mgr_alloc();
    ts_mux_mgr->ts_tstd_mgr = upipe_ts_tstd_mgr_alloc();
    ts_mux_mgr->ts_psig_mgr = upipe_ts_psig_mgr_alloc();

    urefcount_init(upipe_ts_mux_mgr_to_urefcount(ts_mux_mgr),
                   upipe_ts_mux_mgr_free);
    ts_mux_mgr->mgr.refcount = upipe_ts_mux_mgr_to_urefcount(ts_mux_mgr);
    ts_mux_mgr->mgr.signature = UPIPE_TS_MUX_SIGNATURE;
    ts_mux_mgr->mgr.upipe_alloc = upipe_ts_mux_alloc;
    ts_mux_mgr->mgr.upipe_input = NULL;
    ts_mux_mgr->mgr.upipe_control = upipe_ts_mux_control;
    ts_mux_mgr->mgr.upipe_mgr_control = upipe_ts_mux_mgr_control;
    return upipe_ts_mux_mgr_to_upipe_mgr(ts_mux_mgr);
}

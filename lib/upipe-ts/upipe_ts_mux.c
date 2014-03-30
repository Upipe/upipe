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
 * @short Upipe higher-level module muxing elementary streams in a TS
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_output.h>
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
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_bin.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/upipe_ts_join.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/upipe_ts_pes_encaps.h>
#include <upipe-ts/upipe_ts_psi_generator.h>
#include <upipe-ts/upipe_ts_psi_inserter.h>
#include <upipe-ts/upipe_ts_aggregate.h>
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

/** @internal @This is the private context of a ts_mux manager. */
struct upipe_ts_mux_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to ts_join manager */
    struct upipe_mgr *ts_join_mgr;
    /** pointer to ts_agg manager */
    struct upipe_mgr *ts_agg_mgr;

    /* inputs */
    /** pointer to ts_encaps manager */
    struct upipe_mgr *ts_encaps_mgr;

    /* PSI */
    /** pointer to ts_psig manager */
    struct upipe_mgr *ts_psig_mgr;
    /** pointer to ts_psii manager */
    struct upipe_mgr *ts_psii_mgr;

    /* ES */
    /** pointer to ts_tstd manager */
    struct upipe_mgr *ts_tstd_mgr;
    /** pointer to ts_pese manager */
    struct upipe_mgr *ts_pese_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_ts_mux_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_ts_mux_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a ts_mux pipe. */
struct upipe_ts_mux {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** proxy probe */
    struct uprobe probe;
    /** pointer to ts_join inner pipe */
    struct upipe *join;
    /** pointer to ts_psii inner pipe */
    struct upipe *psii;
    /** bin probe for the ts_agg inner pipe */
    struct uprobe agg_probe_bin;
    /** probe for the ts_agg inner pipe */
    struct uprobe agg_probe;
    /** pointer to ts_agg inner pipe (last inner pipe for the bin) */
    struct upipe *agg;
    /** pipe acting as output */
    struct upipe *output;

    /** pointer to ts_psig */
    struct upipe *psig;
    /** pointer to ts_psii_sub dealing with PAT */
    struct upipe *pat_psii;

    /** current conformance */
    enum upipe_ts_conformance conformance;
    /** interval between PATs */
    uint64_t pat_interval;
    /** default interval between PMTs */
    uint64_t pmt_interval;
    /** default interval between PCRs */
    uint64_t pcr_interval;
    /** last attributed automatic SID */
    uint16_t sid_auto;
    /** last attributed automatic PID */
    uint16_t pid_auto;

    /** start date (system clock) */
    uint64_t start_cr_sys;
    /** octetrate reserved for padding (and emergency situation) */
    uint64_t padding_octetrate;
    /** total octetrate including overheads, PMTs and PAT */
    uint64_t total_octetrate;
    /** true if the mux octetrate is automatically assigned */
    bool octetrate_auto;

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
UPIPE_HELPER_UREF_MGR(upipe_ts_mux, uref_mgr)
UPIPE_HELPER_BIN(upipe_ts_mux, agg_probe_bin, agg, output)

UBASE_FROM_TO(upipe_ts_mux, urefcount, urefcount_real, urefcount_real)

/** @hidden */
static void upipe_ts_mux_init(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_update(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_change(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_start(struct upipe *upipe);
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
    /** pointer to ts_program_psig */
    struct upipe *program_psig;
    /** pointer to ts_psii_sub dealing with PMT */
    struct upipe *pmt_psii;

    /** start date (system clock), used to bootstrap the PAT */
    uint64_t start_cr_sys;
    /** total octetrate including overheads and PMT */
    uint64_t total_octetrate;

    /** interval between PMTs */
    uint64_t pmt_interval;
    /** interval between PCRs */
    uint64_t pcr_interval;

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

UBASE_FROM_TO(upipe_ts_mux_program, urefcount, urefcount_real, urefcount_real)

UPIPE_HELPER_SUBPIPE(upipe_ts_mux, upipe_ts_mux_program, program,
                     program_mgr, programs, uchain)

/** @hidden */
static void upipe_ts_mux_program_change(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_program_start(struct upipe *upipe);
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

    /** start date (system clock), used to bootstrap the PMT */
    uint64_t start_cr_sys;

    /** proxy probe */
    struct uprobe probe;
    /** pointer to ts_psig_flow */
    struct upipe *psig_flow;
    /** pointer to ts_tstd */
    struct upipe *tstd;
    /** pointer to ts_encaps */
    struct upipe *encaps;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux_input, upipe, UPIPE_TS_MUX_INPUT_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_mux_input, urefcount,
                       upipe_ts_mux_input_no_input)
UPIPE_HELPER_VOID(upipe_ts_mux_input)

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
static enum ubase_err upipe_ts_mux_input_probe(struct uprobe *uprobe,
                                               struct upipe *inner,
                                               int event, va_list args)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        container_of(uprobe, struct upipe_ts_mux_input, probe);
    struct upipe *upipe = upipe_ts_mux_input_to_upipe(upipe_ts_mux_input);

    if (event == UPROBE_NEW_FLOW_DEF)
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
    upipe_ts_mux_input->input_type = UPIPE_TS_MUX_INPUT_OTHER;
    upipe_ts_mux_input->pcr = false;
    upipe_ts_mux_input->pid = 8192;
    upipe_ts_mux_input->octetrate = 0;
    upipe_ts_mux_input->buffer_duration = 0;
    upipe_ts_mux_input->start_cr_sys = UINT64_MAX;
    upipe_ts_mux_input->total_octetrate = 0;
    upipe_ts_mux_input->psig_flow = upipe_ts_mux_input->tstd =
        upipe_ts_mux_input->encaps = NULL;

    upipe_ts_mux_input_init_sub(upipe);
    uprobe_init(&upipe_ts_mux_input->probe, upipe_ts_mux_input_probe, NULL);
    upipe_ts_mux_input->probe.refcount =
        upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input);
    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)->mgr);
    struct upipe *pes_encaps, *join;
    if (unlikely((upipe_ts_mux_input->psig_flow =
                  upipe_void_alloc_sub(program->program_psig,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "psig flow"))) == NULL ||
                (upipe_ts_mux_input->tstd =
                  upipe_void_alloc(ts_mux_mgr->ts_tstd_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_output_alloc(uprobe_use(&upipe_ts_mux_input->probe)),
                             UPROBE_LOG_VERBOSE, "tstd"))) == NULL ||
                (pes_encaps =
                  upipe_void_alloc_output(upipe_ts_mux_input->tstd,
                         ts_mux_mgr->ts_pese_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_output_alloc(uprobe_use(&upipe_ts_mux_input->probe)),
                             UPROBE_LOG_VERBOSE, "pes encaps"))) == NULL ||
                 (upipe_ts_mux_input->encaps =
                  upipe_void_alloc_output(pes_encaps,
                         ts_mux_mgr->ts_encaps_mgr,
                         uprobe_pfx_alloc_va(
                             uprobe_output_alloc(uprobe_use(&upipe_ts_mux_input->probe)),
                             UPROBE_LOG_VERBOSE, "encaps"))) == NULL ||
                 (join = upipe_void_alloc_output_sub(upipe_ts_mux_input->encaps,
                         upipe_ts_mux->join,
                         uprobe_pfx_alloc_va(
                             uprobe_use(&upipe_ts_mux_input->probe),
                             UPROBE_LOG_VERBOSE, "join"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    upipe_release(pes_encaps);
    upipe_release(join);
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
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);

    if (unlikely(upipe_ts_mux_input->start_cr_sys == UINT64_MAX &&
                 ubase_check(uref_clock_get_dts_sys(uref,
                     &upipe_ts_mux_input->start_cr_sys)))) {
        size_t uref_size = 0;
        uref_block_size(uref, &uref_size);

        upipe_ts_mux_input->start_cr_sys -=
            upipe_ts_mux_input->buffer_duration +
            (uint64_t)uref_size * UCLOCK_FREQ / upipe_ts_mux_input->octetrate;

        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_input_mgr(upipe->mgr);
        upipe_ts_mux_program_start(upipe_ts_mux_program_to_upipe(program));
    }

    if (unlikely(upipe_ts_mux_input->tstd == NULL)) {
        uref_free(uref);
        return;
    }
    upipe_input(upipe_ts_mux_input->tstd, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_input_set_flow_def(struct upipe *upipe,
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
    if (!ubase_check(uref_block_flow_get_octetrate(flow_def, &octetrate))) {
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

    if (strstr(def, ".pic.sub.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_OTHER;
        if (!ubase_ncmp(def, "block.dvb_teletext.")) {
            buffer_size = BS_TELX;
            max_delay = MAX_DELAY_TELX;
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

            struct urational fps;
            if (ubase_check(uref_pic_flow_get_fps(flow_def, &fps))) {
                /* PES header overhead */
                pes_overhead += PES_HEADER_SIZE_TELX * (fps.num + fps.den - 1) /
                                fps.den;
            }

        } else if (!ubase_ncmp(def, "block.dvb_subtitle.")) {
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
            pes_overhead += PES_HEADER_SIZE_PTS * 30;
        }

    } else if (strstr(def, ".pic.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_VIDEO;
        if (!ubase_ncmp(def, "block.mpeg1video.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_VIDEO_MPEG1))
        } else if (!ubase_ncmp(def, "block.mpeg2video.")) {
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                                PMT_STREAMTYPE_VIDEO_MPEG2));
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

        struct urational fps;
        if (ubase_check(uref_pic_flow_get_fps(flow_def, &fps))) {
            /* PES header overhead */
            pes_overhead += PES_HEADER_SIZE_PTSDTS * (fps.num + fps.den - 1) /
                            fps.den;
            /* At worst we'll have 183 octets wasted per frame, if all frames
             * are I-frames or if we don't overlap. This includes PCR overhead.
             */
            pes_overhead += TS_SIZE * (fps.num + fps.den - 1) / fps.den;
        }

    } else if (strstr(def, ".sound.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_AUDIO;
        uint64_t pes_min_duration = DEFAULT_AUDIO_PES_MIN_DURATION;

        if (!ubase_ncmp(def, "block.mp2.") || !ubase_ncmp(def, "block.mp3.")) {
            buffer_size = BS_ADTS_2;
            UBASE_FATAL(upipe, uref_ts_flow_set_stream_type(flow_def_dup,
                                               PMT_STREAMTYPE_AUDIO_MPEG2));
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

        uint64_t rate, samples;
        if (ubase_check(uref_sound_flow_get_rate(flow_def, &rate)) &&
            ubase_check(uref_sound_flow_get_samples(flow_def, &samples))) {
            unsigned int nb_frames = 1;
            while (samples * nb_frames * UCLOCK_FREQ / rate < pes_min_duration)
                nb_frames++;
            samples *= nb_frames;
            /* PES header overhead */
            pes_overhead += PES_HEADER_SIZE_PTS * (rate + samples - 1) /
                            samples;
            /* TS padding overhead */
            pes_overhead += TS_SIZE * (rate + samples - 1) / samples;
        }
    }

    if (buffer_size > max_delay * octetrate / UCLOCK_FREQ) {
        buffer_size = max_delay * octetrate / UCLOCK_FREQ;
        upipe_warn_va(upipe, "lowering buffer size to %"PRIu64" octets to comply with T-STD max retention", buffer_size);
    }
    UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def_dup, octetrate));
    UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def_dup,
                                                       buffer_size));
    UBASE_FATAL(upipe, uref_ts_flow_set_max_delay(flow_def_dup, max_delay));
    upipe_ts_mux_input->buffer_duration = UCLOCK_FREQ * buffer_size / octetrate;

    uint64_t ts_overhead = TS_HEADER_SIZE *
        (octetrate + pes_overhead + TS_SIZE - TS_HEADER_SIZE - 1) /
        (TS_SIZE - TS_HEADER_SIZE);
    uint64_t ts_delay;
    if (!ubase_check(uref_ts_flow_get_ts_delay(flow_def, &ts_delay)))
        UBASE_FATAL(upipe, uref_ts_flow_set_ts_delay(flow_def_dup,
                (uint64_t)T_STD_TS_BUFFER * UCLOCK_FREQ /
                (octetrate + pes_overhead + ts_overhead)));

    uint64_t pid = 0;
    if (ubase_check(uref_ts_flow_get_pid(flow_def, &pid)) &&
        pid != upipe_ts_mux_input->pid &&
        upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(upipe_ts_mux), pid)) {
        upipe_warn_va(upipe_ts_mux_to_upipe(upipe_ts_mux),
                      "PID %"PRIu64" already exists", pid);
        pid = 0;
    }
    if (pid == 0) {
        do {
            pid = upipe_ts_mux->pid_auto++;
        } while (upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(upipe_ts_mux),
                                       pid));
        if (pid >= MAX_PIDS) {
            uref_free(flow_def_dup);
            return UBASE_ERR_BUSY;
        } else
            UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def_dup, pid));
    }

    if (!ubase_check(upipe_set_flow_def(upipe_ts_mux_input->tstd,
                                            flow_def_dup)) ||
        !ubase_check(uref_flow_set_def(flow_def_dup, "void.")) ||
        !ubase_check(upipe_set_flow_def(upipe_ts_mux_input->psig_flow,
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

    upipe_notice_va(upipe, "adding %s on PID %"PRIu64" (%"PRIu64" bits/s)",
                    def, pid, octetrate * 8);
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
static enum ubase_err upipe_ts_mux_input_control(struct upipe *upipe,
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

        default:
            return UBASE_ERR_UNHANDLED;
    }
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

    if (upipe_ts_mux_input->psig_flow != NULL)
        upipe_release(upipe_ts_mux_input->psig_flow);
    if (upipe_ts_mux_input->tstd != NULL)
        upipe_release(upipe_ts_mux_input->tstd);
    if (upipe_ts_mux_input->encaps != NULL)
        upipe_release(upipe_ts_mux_input->encaps);

    upipe_ts_mux_input_clean_sub(upipe);
    if (!upipe_single(upipe_ts_mux_program_to_upipe(program)))
        upipe_ts_mux_program_change(upipe_ts_mux_program_to_upipe(program));
    urefcount_release(upipe_ts_mux_input_to_urefcount_real(upipe_ts_mux_input));
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
static enum ubase_err upipe_ts_mux_program_probe(struct uprobe *uprobe,
                                                 struct upipe *inner,
                                                 int event, va_list args)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        container_of(uprobe, struct upipe_ts_mux_program, probe);
    struct upipe *upipe = upipe_ts_mux_program_to_upipe(upipe_ts_mux_program);

    if (event == UPROBE_NEW_FLOW_DEF)
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
    if (unlikely(!ubase_check(upipe_ts_mux_check_uref_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)))))
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
    upipe_ts_mux_program_init_input_mgr(upipe);
    upipe_ts_mux_program_init_sub_inputs(upipe);
    upipe_ts_mux_program->sid = 0;
    upipe_ts_mux_program->pmt_pid = 8192;
    upipe_ts_mux_program->start_cr_sys = UINT64_MAX;
    upipe_ts_mux_program->pmt_interval = upipe_ts_mux->pmt_interval;
    upipe_ts_mux_program->pcr_interval = upipe_ts_mux->pcr_interval;
    upipe_ts_mux_program->total_octetrate = (uint64_t)TS_SIZE *
        ((UCLOCK_FREQ + upipe_ts_mux_program->pmt_interval - 1) /
         upipe_ts_mux_program->pmt_interval);

    upipe_ts_mux_program_init_sub(upipe);
    uprobe_init(&upipe_ts_mux_program->probe, upipe_ts_mux_program_probe, NULL);
    upipe_ts_mux_program->probe.refcount =
        upipe_ts_mux_program_to_urefcount_real(upipe_ts_mux_program);
    upipe_throw_ready(upipe);

    if (unlikely((upipe_ts_mux_program->program_psig =
                  upipe_void_alloc_sub(upipe_ts_mux->psig,
                         uprobe_pfx_alloc(
                             uprobe_output_alloc(uprobe_use(&upipe_ts_mux_program->probe)),
                             UPROBE_LOG_VERBOSE, "psig program"))) == NULL ||
                 (upipe_ts_mux_program->pmt_psii =
                  upipe_void_alloc_output_sub(
                         upipe_ts_mux_program->program_psig,
                         upipe_ts_mux->psii,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_ts_mux_program->probe),
                             UPROBE_LOG_VERBOSE, "pmt psii"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }
    upipe_ts_psii_sub_set_interval(upipe_ts_mux_program->pmt_psii,
                                   upipe_ts_mux_program->pmt_interval);
    return upipe;
}

/** @This calculates the total octetrate used by a program and updates the
 * aggregate subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_update(struct upipe *upipe)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    uint64_t total_octetrate = (uint64_t)TS_SIZE *
        (((UCLOCK_FREQ + upipe_ts_mux_program->pmt_interval - 1) /
           upipe_ts_mux_program->pmt_interval) +
         ((UCLOCK_FREQ + upipe_ts_mux_program->pcr_interval - 1) /
          upipe_ts_mux_program->pcr_interval));

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
        upipe_ts_psig_program_set_pcr_pid(upipe_ts_mux_program->program_psig,
                                          pcr_pid);
    }

    upipe_ts_mux_program_start(upipe);
}

/** @This is called when the first packet arrives on an input.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_start(struct upipe *upipe)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);

    upipe_ts_mux_program_update(upipe);

    uint64_t earliest_cr_sys = UINT64_MAX;
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        if (input->start_cr_sys == UINT64_MAX)
            return; /* an input is not ready yet */
        if (input->start_cr_sys < earliest_cr_sys)
            earliest_cr_sys = input->start_cr_sys;
    }

    bool first = upipe_ts_mux_program->start_cr_sys == UINT64_MAX;
    upipe_ts_mux_program->start_cr_sys = earliest_cr_sys - PMT_OFFSET;

    struct upipe_ts_mux *upipe_ts_mux =
        upipe_ts_mux_from_program_mgr(upipe->mgr);
    if (first)
        upipe_ts_mux_start(upipe_ts_mux_to_upipe(upipe_ts_mux));

    /* Build a new PMT. */
    struct uref *uref = uref_alloc(upipe_ts_mux->uref_mgr);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (first) {
         uref_clock_set_cr_sys(uref, earliest_cr_sys);
         uref_clock_set_cr_dts_delay(uref, PMT_OFFSET);
    }
    /* FIXME in case of deletion PMT will be output too early */
    upipe_input(upipe_ts_mux_program->program_psig, uref, NULL);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_program_set_flow_def(struct upipe *upipe,
                                                        struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    struct upipe_ts_mux *upipe_ts_mux =
        upipe_ts_mux_from_program_mgr(upipe->mgr);
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    uint64_t sid = 0;
    if (ubase_check(uref_flow_get_id(flow_def, &sid)) &&
        upipe_ts_mux_find_sid(upipe_ts_mux_to_upipe(upipe_ts_mux), sid)) {
        upipe_warn_va(upipe_ts_mux_to_upipe(upipe_ts_mux),
                      "SID %"PRIu64" already exists", sid);
        sid = 0;
    }
    if (sid == 0) {
        do {
            sid = upipe_ts_mux->sid_auto++;
        } while (upipe_ts_mux_find_sid(upipe_ts_mux_to_upipe(upipe_ts_mux),
                                       sid));
        if (sid >= MAX_SIDS) {
            uref_free(flow_def_dup);
            return UBASE_ERR_BUSY;
        } else
            UBASE_FATAL(upipe, uref_flow_set_id(flow_def_dup, sid));
    }

    uint64_t pid = 0;
    if (ubase_check(uref_ts_flow_get_pid(flow_def, &pid)) &&
        upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(upipe_ts_mux), pid)) {
        upipe_warn_va(upipe_ts_mux_to_upipe(upipe_ts_mux),
                      "PID %"PRIu64" already exists", pid);
        pid = 0;
    }
    if (pid == 0) {
        do {
            pid = upipe_ts_mux->pid_auto++;
        } while (upipe_ts_mux_find_pid(upipe_ts_mux_to_upipe(upipe_ts_mux),
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

    if (!ubase_check(upipe_set_flow_def(upipe_ts_mux_program->program_psig,
                                            flow_def_dup))) {
        uref_free(flow_def_dup);
        return UBASE_ERR_INVALID;
    }

    uref_free(flow_def_dup);
    upipe_ts_mux_program->sid = sid;
    upipe_ts_mux_program->pmt_pid = pid;

    upipe_notice_va(upipe, "adding program %"PRIu64" on PID %"PRIu64, sid, pid);
    upipe_ts_mux_change(upipe_ts_mux_to_upipe(upipe_ts_mux));
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static enum ubase_err
    upipe_ts_mux_program_get_pmt_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->pmt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PMT interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static enum ubase_err
    upipe_ts_mux_program_set_pmt_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->pmt_interval = interval;
    if (upipe_ts_mux_program->pmt_psii != NULL)
        upipe_ts_psii_sub_set_interval(upipe_ts_mux_program->pmt_psii,
                                       interval);
    upipe_ts_mux_program_update(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static enum ubase_err
    upipe_ts_mux_program_get_pcr_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->pcr_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PCR interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static enum ubase_err
    upipe_ts_mux_program_set_pcr_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->pcr_interval = interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        if (input->pcr && input->encaps != NULL)
            upipe_ts_mux_set_pcr_interval(input->encaps, interval);
    }
    upipe_ts_mux_program_update(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts_mux_program pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_program_control(struct upipe *upipe,
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

        default:
            return false;
    }
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
    struct upipe_ts_mux *mux = upipe_ts_mux_from_program_mgr(upipe->mgr);

    if (upipe_ts_mux_program->program_psig != NULL)
        upipe_release(upipe_ts_mux_program->program_psig);
    if (upipe_ts_mux_program->pmt_psii != NULL)
        upipe_release(upipe_ts_mux_program->pmt_psii);

    upipe_ts_mux_program_clean_sub_inputs(upipe);
    upipe_ts_mux_program_clean_sub(upipe);
    if (!upipe_single(upipe_ts_mux_to_upipe(mux)))
        upipe_ts_mux_change(upipe_ts_mux_to_upipe(mux));
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
 * upipe_ts_mux structure handling (derived from upipe structure)
 */

/** @internal @This catches the events from agg inner pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_agg_probe(struct uprobe *uprobe,
                                             struct upipe *inner,
                                             int event, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux =
        container_of(uprobe, struct upipe_ts_mux, agg_probe);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This catches the events from inner pipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param inner pointer to the inner pipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_probe(struct uprobe *uprobe,
                                         struct upipe *inner,
                                         int event, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux =
        container_of(uprobe, struct upipe_ts_mux, probe);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    if (event == UPROBE_NEW_FLOW_DEF)
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
    upipe_ts_mux_init_uref_mgr(upipe);
    upipe_ts_mux_init_bin(upipe, upipe_ts_mux_to_urefcount_real(upipe_ts_mux));
    upipe_ts_mux_init_program_mgr(upipe);
    upipe_ts_mux_init_sub_programs(upipe);
    upipe_ts_mux->psig = upipe_ts_mux->join = upipe_ts_mux->pat_psii = NULL;
    upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_ISO;
    upipe_ts_mux->pat_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pmt_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pcr_interval = DEFAULT_PCR_INTERVAL;
    upipe_ts_mux->sid_auto = DEFAULT_SID_AUTO;
    upipe_ts_mux->pid_auto = DEFAULT_PID_AUTO;
    upipe_ts_mux->start_cr_sys = UINT64_MAX;
    upipe_ts_mux->padding_octetrate = 0;
    upipe_ts_mux->total_octetrate = (uint64_t)TS_SIZE *
        ((UCLOCK_FREQ + upipe_ts_mux->pat_interval - 1) /
         upipe_ts_mux->pat_interval);
    upipe_ts_mux->octetrate_auto = true;

    uprobe_init(&upipe_ts_mux->probe, upipe_ts_mux_probe, NULL);
    upipe_ts_mux->probe.refcount = upipe_ts_mux_to_urefcount_real(upipe_ts_mux);
    uprobe_init(&upipe_ts_mux->agg_probe, upipe_ts_mux_agg_probe,
                &upipe_ts_mux->agg_probe_bin);
    upipe_ts_mux->agg_probe.refcount =
        upipe_ts_mux_to_urefcount_real(upipe_ts_mux);

    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);
    if (unlikely((upipe_ts_mux->psig =
                  upipe_void_alloc(ts_mux_mgr->ts_psig_mgr,
                         uprobe_pfx_alloc(
                             uprobe_output_alloc(
                                 uprobe_use(&upipe_ts_mux->probe)),
                             UPROBE_LOG_VERBOSE, "psig"))) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return upipe;
    }

    if (ubase_check(upipe_ts_mux_check_uref_mgr(upipe)))
        upipe_ts_mux_init(upipe);
    return upipe;
}

/** @This is called when the uref_mgr is set and the ts_mux can be inited.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_init(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);

    upipe_ts_mux->join =
        upipe_void_alloc(ts_mux_mgr->ts_join_mgr,
             uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(&upipe_ts_mux->probe)),
                              UPROBE_LOG_VERBOSE, "join"));
    if (unlikely(upipe_ts_mux->join == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_ts_mux->psii =
        upipe_void_alloc_output(upipe_ts_mux->join, ts_mux_mgr->ts_psii_mgr,
              uprobe_pfx_alloc(uprobe_output_alloc(uprobe_use(&upipe_ts_mux->probe)),
                               UPROBE_LOG_VERBOSE, "psii"));
    if (unlikely(upipe_ts_mux->psii == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    struct upipe *agg =
        upipe_void_alloc_output(upipe_ts_mux->psii, ts_mux_mgr->ts_agg_mgr,
                           uprobe_pfx_alloc(
                               uprobe_use(&upipe_ts_mux->agg_probe),
                               UPROBE_LOG_VERBOSE, "agg"));
    if (unlikely(agg == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_mux_store_last_inner(upipe, agg);

    if (unlikely(!ubase_check(upipe_ts_mux_set_mode(upipe_ts_mux->agg,
                                        UPIPE_TS_MUX_MODE_CAPPED)) ||
                 (upipe_ts_mux->pat_psii =
                  upipe_void_alloc_output_sub(upipe_ts_mux->psig,
                         upipe_ts_mux->psii,
                         uprobe_pfx_alloc(uprobe_use(&upipe_ts_mux->probe),
                                          UPROBE_LOG_VERBOSE, "pat psii")))
                  == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_psii_sub_set_interval(upipe_ts_mux->pat_psii,
                                   upipe_ts_mux->pat_interval);
}

/** @This calculates the total octetrate used by a stream and updates the
 * aggregate inner pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_update(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    uint64_t total_octetrate = (uint64_t)TS_SIZE *
        ((UCLOCK_FREQ + upipe_ts_mux->pat_interval - 1) /
         upipe_ts_mux->pat_interval) + upipe_ts_mux->padding_octetrate;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        total_octetrate += program->total_octetrate;
    }

    if (total_octetrate != upipe_ts_mux->total_octetrate) {
        upipe_ts_mux->total_octetrate = total_octetrate;
        if (upipe_ts_mux->octetrate_auto && upipe_ts_mux->agg != NULL)
            upipe_ts_mux_set_octetrate(upipe_ts_mux->agg,
                                       upipe_ts_mux->total_octetrate);
    }
}

/** @This is called when the transport stream definition is changed (program
 * added or deleted).
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_change(struct upipe *upipe)
{
    upipe_ts_mux_start(upipe);
}

/** @This is called when the first PMT arrives on a program.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_start(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);

    upipe_ts_mux_update(upipe);

    uint64_t earliest_cr_sys = UINT64_MAX;
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->start_cr_sys == UINT64_MAX)
            return; /* a program is not ready yet */
        if (program->start_cr_sys < earliest_cr_sys)
            earliest_cr_sys = program->start_cr_sys;
    }

    bool first = upipe_ts_mux->start_cr_sys == UINT64_MAX;
    upipe_ts_mux->start_cr_sys = earliest_cr_sys;

    /* Build a new PAT. */
    struct uref *uref = uref_alloc(upipe_ts_mux->uref_mgr);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (first) {
         uref_clock_set_cr_sys(uref, earliest_cr_sys);
         uref_clock_set_cr_dts_delay(uref, PAT_OFFSET);
    }
    /* FIXME in case of deletion PAT will be output too early */
    upipe_input(upipe_ts_mux->psig, uref, NULL);
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

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_set_flow_def(struct upipe *upipe,
                                                struct uref *flow_def)
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

    enum ubase_err err = upipe_set_flow_def(upipe_ts_mux->psig, flow_def_dup);
    uref_free(flow_def_dup);
    return err;
}

/** @internal @This returns the current conformance mode. It cannot
 * return CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return an error code
 */
static enum ubase_err
    _upipe_ts_mux_get_conformance(struct upipe *upipe,
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
static enum ubase_err
    _upipe_ts_mux_set_conformance(struct upipe *upipe,
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
        upipe_ts_mux->pat_interval = max_psi_interval;
    if (upipe_ts_mux->pmt_interval > max_psi_interval)
        upipe_ts_mux->pmt_interval = max_psi_interval;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->pmt_interval > max_psi_interval)
            program->pmt_interval = max_psi_interval;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static enum ubase_err _upipe_ts_mux_get_pat_interval(struct upipe *upipe,
                                                     uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pat_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PAT interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static enum ubase_err _upipe_ts_mux_set_pat_interval(struct upipe *upipe,
                                                     uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->pat_interval = interval;
    if (upipe_ts_mux->pat_psii != NULL)
        upipe_ts_psii_sub_set_interval(upipe_ts_mux->pat_psii, interval);
    upipe_ts_mux_update(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static enum ubase_err _upipe_ts_mux_get_pmt_interval(struct upipe *upipe,
                                                     uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pmt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PMT interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static enum ubase_err _upipe_ts_mux_set_pmt_interval(struct upipe *upipe,
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
static enum ubase_err _upipe_ts_mux_get_pcr_interval(struct upipe *upipe,
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
static enum ubase_err _upipe_ts_mux_set_pcr_interval(struct upipe *upipe,
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

/** @internal @This returns the current padding octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static enum ubase_err _upipe_ts_mux_get_padding_octetrate(struct upipe *upipe,
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
static enum ubase_err _upipe_ts_mux_set_padding_octetrate(struct upipe *upipe,
                                                          uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->padding_octetrate = octetrate;
    upipe_ts_mux_update(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static enum ubase_err _upipe_ts_mux_set_octetrate(struct upipe *upipe,
                                                  uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (upipe_ts_mux->agg == NULL)
        return UBASE_ERR_UNHANDLED;

    if (octetrate) {
        upipe_ts_mux->octetrate_auto = false;
        return upipe_ts_mux_set_octetrate(upipe_ts_mux->agg, octetrate);
    }
    upipe_ts_mux->octetrate_auto = true;
    return upipe_ts_mux_set_octetrate(upipe_ts_mux->agg,
                                      upipe_ts_mux->total_octetrate);
}

/** @internal @This processes control commands on a ts_mux pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err upipe_ts_mux_control(struct upipe *upipe,
                                           int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR: {
            UBASE_RETURN(upipe_ts_mux_attach_uref_mgr(upipe))
            /* To create the flow definition. */
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
            if (upipe_ts_mux->join == NULL)
                upipe_ts_mux_init(upipe);
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mux_set_flow_def(upipe, flow_def);
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
        case UPIPE_TS_MUX_SET_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t octetrate = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_octetrate(upipe, octetrate);
        }

        default:
            return upipe_ts_mux_control_bin(upipe, command, args);
    }
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_ts_mux_free(struct urefcount *urefcount_real)
{
    struct upipe_ts_mux *upipe_ts_mux =
        upipe_ts_mux_from_urefcount_real(urefcount_real);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    upipe_throw_dead(upipe);

    uprobe_clean(&upipe_ts_mux->probe);
    uprobe_clean(&upipe_ts_mux->agg_probe);
    urefcount_clean(urefcount_real);
    upipe_ts_mux_clean_uref_mgr(upipe);
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
    if (upipe_ts_mux->psii != NULL)
        upipe_release(upipe_ts_mux->psii);
    if (upipe_ts_mux->join != NULL)
        upipe_release(upipe_ts_mux->join);
    if (upipe_ts_mux->psig != NULL)
        upipe_release(upipe_ts_mux->psig);
    if (upipe_ts_mux->pat_psii != NULL)
        upipe_release(upipe_ts_mux->pat_psii);

    upipe_ts_mux_clean_sub_programs(upipe);
    upipe_ts_mux_clean_bin(upipe);
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
    if (ts_mux_mgr->ts_join_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_join_mgr);
    if (ts_mux_mgr->ts_agg_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_agg_mgr);
    if (ts_mux_mgr->ts_encaps_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_encaps_mgr);
    if (ts_mux_mgr->ts_tstd_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_tstd_mgr);
    if (ts_mux_mgr->ts_pese_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_pese_mgr);
    if (ts_mux_mgr->ts_psig_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_psig_mgr);
    if (ts_mux_mgr->ts_psii_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_psii_mgr);

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
static enum ubase_err upipe_ts_mux_mgr_control(struct upipe_mgr *mgr,
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

        GET_SET_MGR(ts_join, TS_JOIN)
        GET_SET_MGR(ts_agg, TS_AGG)
        GET_SET_MGR(ts_encaps, TS_ENCAPS)
        GET_SET_MGR(ts_pese, TS_PESE)
        GET_SET_MGR(ts_psig, TS_PSIG)
        GET_SET_MGR(ts_psii, TS_PSII)
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

    ts_mux_mgr->ts_join_mgr = upipe_ts_join_mgr_alloc();
    ts_mux_mgr->ts_agg_mgr = upipe_ts_agg_mgr_alloc();
    ts_mux_mgr->ts_encaps_mgr = upipe_ts_encaps_mgr_alloc();
    ts_mux_mgr->ts_tstd_mgr = upipe_ts_tstd_mgr_alloc();
    ts_mux_mgr->ts_pese_mgr = upipe_ts_pese_mgr_alloc();
    ts_mux_mgr->ts_psig_mgr = upipe_ts_psig_mgr_alloc();
    ts_mux_mgr->ts_psii_mgr = upipe_ts_psii_mgr_alloc();

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

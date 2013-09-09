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
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
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
/** T-STD TS buffer */
#define T_STD_TS_BUFFER 512
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
/** max retention time for most streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY UCLOCK_FREQ
/** max retention time for ISO/IEC 14496 streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_14496 (UCLOCK_FREQ * 10)
/** max retention time for still pictures streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_STILL (UCLOCK_FREQ * 60)
/** default TSID */
#define DEFAULT_TSID 1
/** default first automatic SID */
#define DEFAULT_SID_AUTO 1
/** default first automatic PID */
#define DEFAULT_PID_AUTO 256

/** @internal @This is the private context of a ts_mux manager. */
struct upipe_ts_mux_mgr {
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
    /** pointer to ts_pese manager */
    struct upipe_mgr *ts_pese_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

/** @internal @This returns the high-level upipe_mgr structure.
 *
 * @param ts_mux_mgr pointer to the upipe_ts_mux_mgr structure
 * @return pointer to the upipe_mgr structure
 */
static inline struct upipe_mgr *
    upipe_ts_mux_mgr_to_upipe_mgr(struct upipe_ts_mux_mgr *ts_mux_mgr)
{
    return &ts_mux_mgr->mgr;
}

/** @internal @This returns the private upipe_ts_mux_mgr structure.
 *
 * @param mgr description structure of the upipe manager
 * @return pointer to the upipe_ts_mux_mgr structure
 */
static inline struct upipe_ts_mux_mgr *
    upipe_ts_mux_mgr_from_upipe_mgr(struct upipe_mgr *mgr)
{
    return container_of(mgr, struct upipe_ts_mux_mgr, mgr);
}

/** @internal @This is the private context of a ts_mux pipe. */
struct upipe_ts_mux {
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** probe for uref_mgr and ubuf_mgr */
    struct uprobe probe;
    /** pointer to ts_join subpipe */
    struct upipe *join;
    /** pointer to ts_psii subpipe */
    struct upipe *psii;
    /** bin probe for the ts_agg subpipe */
    struct uprobe agg_probe_bin;
    /** probe for the ts_agg subpipe */
    struct uprobe agg_probe;
    /** pointer to ts_agg subpipe (last subpipe for the bin) */
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

    /** start DTS */
    uint64_t start_dts;
    /** start DTS (system clock) */
    uint64_t start_dts_sys;
    /** octetrate reserved for padding (and emergency situation) */
    uint64_t padding_octetrate;
    /** total octetrate including overheads, PMTs and PAT */
    uint64_t total_octetrate;
    /** true if the mux octetrate is automatically assigned */
    bool octetrate_auto;

    /** list of programs */
    struct ulist programs;

    /** manager to create programs */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux, upipe, UPIPE_TS_MUX_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_mux, "void.")
UPIPE_HELPER_UREF_MGR(upipe_ts_mux, uref_mgr)
UPIPE_HELPER_UBUF_MGR(upipe_ts_mux, ubuf_mgr)
UPIPE_HELPER_BIN(upipe_ts_mux, agg_probe_bin, agg, output)

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

/** @internal @This is the private context of a program of a ts_mux pipe. */
struct upipe_ts_mux_program {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** SID */
    uint16_t sid;
    /** PMT PID */
    uint16_t pmt_pid;

    /** pointer to ts_program_psig */
    struct upipe *program_psig;
    /** pointer to ts_psii_sub dealing with PMT */
    struct upipe *pmt_psii;

    /** start DTS, used to bootstrap the PAT */
    uint64_t start_dts;
    /** start DTS (system clock), used to bootstrap the PAT */
    uint64_t start_dts_sys;
    /** total octetrate including overheads and PMT */
    uint64_t total_octetrate;

    /** interval between PMTs */
    uint64_t pmt_interval;
    /** interval between PCRs */
    uint64_t pcr_interval;

    /** list of inputs */
    struct ulist inputs;

    /** manager to create inputs */
    struct upipe_mgr input_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux_program, upipe, UPIPE_TS_MUX_PROGRAM_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_mux_program, "void.")

UPIPE_HELPER_SUBPIPE(upipe_ts_mux, upipe_ts_mux_program, program,
                     program_mgr, programs, uchain)

/** @hidden */
static void upipe_ts_mux_program_change(struct upipe *upipe);
/** @hidden */
static void upipe_ts_mux_program_start(struct upipe *upipe);

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
    /** structure for double-linked lists */
    struct uchain uchain;

    /** input type */
    enum upipe_ts_mux_input_type input_type;
    /** PID */
    uint16_t pid;
    /** octet rate */
    uint64_t octetrate;
    /** true if the output is used for PCR */
    bool pcr;
    /** total octetrate including overheads */
    uint64_t total_octetrate;

    /** start DTS, used to bootstrap the PMT */
    uint64_t start_dts;
    /** start DTS (system clock), used to bootstrap the PMT */
    uint64_t start_dts_sys;

    /** pointer to ts_psig_flow */
    struct upipe *psig_flow;
    /** pointer to ts_pes_encaps */
    struct upipe *pes_encaps;
    /** pointer to ts_encaps */
    struct upipe *encaps;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_mux_input, upipe, UPIPE_TS_MUX_INPUT_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_mux_input, "block.")

UPIPE_HELPER_SUBPIPE(upipe_ts_mux_program, upipe_ts_mux_input, input,
                     input_mgr, inputs, uchain)


/*
 * upipe_ts_mux_input structure handling (derived from upipe structure)
 */

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

    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_mux_input_alloc_flow(mgr, uprobe,
                                                        signature, args,
                                                        &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    const char *def;
    uint64_t octetrate;
    if (unlikely(!uref_flow_get_def(flow_def, &def) ||
                 !uref_block_flow_get_octetrate(flow_def, &octetrate) ||
                 !octetrate)) {
        uref_free(flow_def);
        upipe_ts_mux_input_free_flow(upipe);
        return NULL;
    }
    /* Remember that after the first call to uref_*_set_*, def should no longer
     * be used as it points to data which may have changed location. */

    bool ret = true;
    uint64_t pes_overhead = 0;
    enum upipe_ts_mux_input_type input_type = UPIPE_TS_MUX_INPUT_OTHER;
    if (strstr(def, ".pic.") != NULL) {
        input_type = UPIPE_TS_MUX_INPUT_VIDEO;
        if (!ubase_ncmp(def, "block.mpeg1video.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_VIDEO_MPEG1);
            ret = ret && uref_ts_flow_set_max_delay(flow_def, MAX_DELAY);
        } else if (!ubase_ncmp(def, "block.mpeg2video.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_VIDEO_MPEG2);
            ret = ret && uref_ts_flow_set_max_delay(flow_def, MAX_DELAY);
        } else if (!ubase_ncmp(def, "block.mpeg4.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_VIDEO_MPEG4);
            ret = ret && uref_ts_flow_set_max_delay(flow_def, MAX_DELAY_14496);
        } else if (!ubase_ncmp(def, "block.h264.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_VIDEO_AVC);
            ret = ret && uref_ts_flow_set_max_delay(flow_def, MAX_DELAY_14496);
        }
        ret = ret && uref_ts_flow_set_pes_id(flow_def,
                                             PES_STREAM_ID_VIDEO_MPEG);

        uint64_t max_octetrate = octetrate;
        uref_block_flow_get_max_octetrate(flow_def, &max_octetrate);
        /* ISO/IEC 13818-1 2.4.2.3 */
        ret = ret && uref_ts_flow_set_tb_rate(flow_def, max_octetrate * 6 / 5);

        struct urational fps;
        if (uref_pic_flow_get_fps(flow_def, &fps)) {
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
        if (!ubase_ncmp(def, "block.mp2.") || !ubase_ncmp(def, "block.mp3.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_AUDIO_MPEG2);
            ret = ret && uref_ts_flow_set_pes_id(flow_def,
                                                 PES_STREAM_ID_AUDIO_MPEG);
        } else if (!ubase_ncmp(def, "block.aac.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_AUDIO_ADTS);
            ret = ret && uref_ts_flow_set_pes_id(flow_def,
                                                 PES_STREAM_ID_AUDIO_MPEG);
        } else if (!ubase_ncmp(def, "block.ac3.")) {
            ret = ret &&
                  uref_ts_flow_set_stream_type(flow_def,
                                               PMT_STREAMTYPE_PRIVATE_PES);
            ret = ret && uref_ts_flow_set_pes_id(flow_def,
                                                 PES_STREAM_ID_PRIVATE_1);
            uint8_t ac3_descriptor[DESC6A_HEADER_SIZE];
            desc6a_init(ac3_descriptor);
            desc_set_length(ac3_descriptor,
                            DESC6A_HEADER_SIZE - DESC_HEADER_SIZE);
            desc6a_clear_flags(ac3_descriptor);
            ret = ret && uref_ts_flow_set_descriptors(flow_def, ac3_descriptor,
                                                      DESC6A_HEADER_SIZE);
        }
        ret = ret && uref_ts_flow_set_tb_rate(flow_def, TB_RATE_AUDIO);
        ret = ret && uref_ts_flow_set_max_delay(flow_def, MAX_DELAY);
        uint64_t pes_min_duration = DEFAULT_AUDIO_PES_MIN_DURATION;
        if (!uref_ts_flow_get_pes_min_duration(flow_def, &pes_min_duration))
            ret = ret && uref_ts_flow_set_pes_min_duration(flow_def,
                                                           pes_min_duration);

        uint64_t rate, samples;
        if (uref_sound_flow_get_rate(flow_def, &rate) &&
            uref_sound_flow_get_samples(flow_def, &samples)) {
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

    } else if (strstr(def, ".subpic.") != NULL) {
    }

    uint64_t ts_overhead = TS_HEADER_SIZE *
        (octetrate + pes_overhead + TS_SIZE - TS_HEADER_SIZE - 1) /
        (TS_SIZE - TS_HEADER_SIZE);
    uint64_t ts_delay;
    if (!uref_ts_flow_get_ts_delay(flow_def, &ts_delay))
        ret = ret && uref_ts_flow_set_ts_delay(flow_def,
                (uint64_t)T_STD_TS_BUFFER * UCLOCK_FREQ /
                (octetrate + pes_overhead + ts_overhead));

    uint64_t pid = 0;
    if (uref_ts_flow_get_pid(flow_def, &pid) &&
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
        if (pid >= MAX_PIDS)
            ret = false;
        else
            ret = ret && uref_ts_flow_set_pid(flow_def, pid);
    }

    if (unlikely(!ret)) {
        uref_free(flow_def);
        upipe_ts_mux_input_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    upipe_ts_mux_input->input_type = input_type;
    upipe_ts_mux_input->pcr = false;
    upipe_ts_mux_input->pid = pid;
    upipe_ts_mux_input->octetrate = octetrate;
    upipe_ts_mux_input->start_dts = UINT64_MAX;
    upipe_ts_mux_input->start_dts_sys = UINT64_MAX;
    upipe_ts_mux_input->total_octetrate = octetrate +
                                          pes_overhead + ts_overhead;
    upipe_ts_mux_input->psig_flow = upipe_ts_mux_input->pes_encaps =
        upipe_ts_mux_input->encaps = NULL;

    upipe_ts_mux_input_init_sub(upipe);
    upipe_use(upipe_ts_mux_program_to_upipe(program));
    upipe_throw_ready(upipe);

    uref_flow_get_def(flow_def, &def);
    upipe_notice_va(upipe, "adding %s on PID %"PRIu16" (%"PRIu64" bits/s)",
                    def, pid, octetrate * 8);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux)->mgr);
    upipe_ts_mux_input->pes_encaps =
        upipe_flow_alloc(ts_mux_mgr->ts_pese_mgr,
                         uprobe_pfx_adhoc_alloc_va(&upipe_ts_mux->probe,
                                                   UPROBE_LOG_DEBUG,
                                                   "pes encaps %"PRIu64, pid),
                         flow_def);
    struct uref *flow_def_next;
    /* We do not take into account PES overhead here: the PES header isn't
     * part of the VBV buffer calculation stuff, and this allows to send the
     * packets a bit earlier. */
    if (unlikely(!upipe_get_flow_def(upipe_ts_mux_input->pes_encaps,
                                     &flow_def_next) ||
                 (upipe_ts_mux_input->encaps =
                  upipe_flow_alloc(ts_mux_mgr->ts_encaps_mgr,
                         uprobe_pfx_adhoc_alloc_va(&upipe_ts_mux->probe,
                                                   UPROBE_LOG_DEBUG,
                                                   "encaps %"PRIu64, pid),
                         flow_def_next)) == NULL)) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return upipe;
    }
    upipe_set_output(upipe_ts_mux_input->pes_encaps,
                     upipe_ts_mux_input->encaps);

    struct upipe *join;
    if (unlikely(!upipe_get_flow_def(upipe_ts_mux_input->encaps,
                                     &flow_def_next) ||
                 (join = upipe_flow_alloc_sub(upipe_ts_mux->join,
                         uprobe_pfx_adhoc_alloc_va(&upipe_ts_mux->probe,
                                                   UPROBE_LOG_DEBUG,
                                                   "join %"PRIu64, pid),
                         flow_def_next)) == NULL)) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return upipe;
    }
    upipe_set_output(upipe_ts_mux_input->encaps, join);

    upipe_release(join);

    if (!uref_flow_set_def(flow_def, "void.") ||
        (upipe_ts_mux_input->psig_flow =
            upipe_flow_alloc_sub(program->program_psig,
                 uprobe_pfx_adhoc_alloc_va(&upipe_ts_mux->probe,
                                           UPROBE_LOG_DEBUG,
                                           "psig flow %"PRIu64, pid),
                 flow_def)) == NULL) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UPROBE_ERR_INVALID);
        return upipe;
    }
    uref_free(flow_def);

    upipe_ts_mux_program_change(upipe_ts_mux_program_to_upipe(program));
    return upipe;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_mux_input_input(struct upipe *upipe, struct uref *uref,
                                     struct upump *upump)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);

    if (unlikely(upipe_ts_mux_input->start_dts == UINT64_MAX &&
                 uref_clock_get_dts(uref, &upipe_ts_mux_input->start_dts))) {
        uint64_t delay = 0;
        uref_clock_get_vbv_delay(uref, &delay);
        size_t uref_size = 0;
        uref_block_size(uref, &uref_size);

        upipe_ts_mux_input->start_dts -= delay +
            (uint64_t)uref_size * UCLOCK_FREQ / upipe_ts_mux_input->octetrate;
        if (uref_clock_get_dts_sys(uref, &upipe_ts_mux_input->start_dts_sys))
            upipe_ts_mux_input->start_dts_sys -= delay +
                (uint64_t)uref_size * UCLOCK_FREQ /
                upipe_ts_mux_input->octetrate;

        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_input_mgr(upipe->mgr);
        upipe_ts_mux_program_start(upipe_ts_mux_program_to_upipe(program));
    }

    if (unlikely(upipe_ts_mux_input->pes_encaps == NULL)) {
        uref_free(uref);
        return;
    }
    upipe_input(upipe_ts_mux_input->pes_encaps, uref, upump);
}

/** @internal @This processes control commands on a ts_mux_input
 * pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_mux_input_control(struct upipe *upipe,
                                       enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_input_get_super(upipe, p);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_input_free(struct upipe *upipe)
{
    struct upipe_ts_mux_input *upipe_ts_mux_input =
        upipe_ts_mux_input_from_upipe(upipe);
    struct upipe_ts_mux_program *program =
        upipe_ts_mux_program_from_input_mgr(upipe->mgr);

    if (upipe_ts_mux_input->psig_flow != NULL)
        upipe_release(upipe_ts_mux_input->psig_flow);
    if (upipe_ts_mux_input->pes_encaps != NULL)
        upipe_release(upipe_ts_mux_input->pes_encaps);
    if (upipe_ts_mux_input->encaps != NULL)
        upipe_release(upipe_ts_mux_input->encaps);
    upipe_throw_dead(upipe);

    upipe_ts_mux_input_clean_sub(upipe);
    upipe_ts_mux_input_free_flow(upipe);

    if (!upipe_single(upipe_ts_mux_program_to_upipe(program)))
        upipe_ts_mux_program_change(upipe_ts_mux_program_to_upipe(program));
    upipe_release(upipe_ts_mux_program_to_upipe(program));
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
    input_mgr->signature = UPIPE_TS_MUX_INPUT_SIGNATURE;
    input_mgr->upipe_alloc = upipe_ts_mux_input_alloc;
    input_mgr->upipe_input = upipe_ts_mux_input_input;
    input_mgr->upipe_control = upipe_ts_mux_input_control;
    input_mgr->upipe_free = upipe_ts_mux_input_free;
    input_mgr->upipe_mgr_free = NULL;
}


/*
 * upipe_ts_mux_program structure handling (derived from upipe structure)
 */

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
        upipe_throw_need_uref_mgr(upipe_ts_mux_to_upipe(upipe_ts_mux));
    if (unlikely(upipe_ts_mux->uref_mgr == NULL))
        return NULL;

    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_mux_program_alloc_flow(mgr, uprobe,
                                                          signature,
                                                          args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    bool ret = true;
    uint64_t sid = 0;
    if (uref_ts_flow_get_sid(flow_def, &sid) &&
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
        if (sid >= MAX_SIDS)
            ret = false;
        else
            ret = ret && uref_ts_flow_set_sid(flow_def, sid);
    }

    uint64_t pid = 0;
    if (uref_ts_flow_get_pid(flow_def, &pid) &&
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
        if (pid >= MAX_PIDS)
            ret = false;
        else
            ret = ret && uref_ts_flow_set_pid(flow_def, pid);
    }

    uint64_t octetrate;
    if (!uref_block_flow_get_octetrate(flow_def, &octetrate))
        ret = ret && uref_block_flow_set_octetrate(flow_def, TB_RATE_PSI);
    ret = ret && uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI);

    if (unlikely(!ret)) {
        uref_free(flow_def);
        upipe_ts_mux_program_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program_init_input_mgr(upipe);
    upipe_ts_mux_program_init_sub_inputs(upipe);
    upipe_ts_mux_program->sid = sid;
    upipe_ts_mux_program->pmt_pid = pid;
    upipe_ts_mux_program->start_dts = UINT64_MAX;
    upipe_ts_mux_program->start_dts_sys = UINT64_MAX;
    upipe_ts_mux_program->pmt_interval = upipe_ts_mux->pmt_interval;
    upipe_ts_mux_program->pcr_interval = upipe_ts_mux->pcr_interval;
    upipe_ts_mux_program->total_octetrate = (uint64_t)TS_SIZE *
        ((UCLOCK_FREQ + upipe_ts_mux_program->pmt_interval - 1) /
         upipe_ts_mux_program->pmt_interval);

    upipe_ts_mux_program_init_sub(upipe);
    upipe_use(upipe_ts_mux_to_upipe(upipe_ts_mux));
    upipe_throw_ready(upipe);

    upipe_ts_mux_program->program_psig =
        upipe_flow_alloc_sub(upipe_ts_mux->psig,
                             uprobe_pfx_adhoc_alloc(&upipe_ts_mux->probe,
                                                    UPROBE_LOG_DEBUG,
                                                    "psig program"),
                             flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_mux_program->program_psig == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_INVALID);
        return upipe;
    }

    if (unlikely(!upipe_get_flow_def(upipe_ts_mux_program->program_psig,
                                     &flow_def) ||
                 (upipe_ts_mux_program->pmt_psii =
                  upipe_flow_alloc_sub(upipe_ts_mux->psii,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_mux->probe,
                                                UPROBE_LOG_DEBUG, "pmt psii"),
                         flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return upipe;
    }
    upipe_ts_psii_sub_set_interval(upipe_ts_mux_program->pmt_psii,
                                   upipe_ts_mux_program->pmt_interval);
    upipe_set_output(upipe_ts_mux_program->program_psig,
                     upipe_ts_mux_program->pmt_psii);

    upipe_ts_mux_change(upipe_ts_mux_to_upipe(upipe_ts_mux));
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

    uint64_t earliest_dts = UINT64_MAX, earliest_dts_sys = UINT64_MAX;
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux_program->inputs, uchain) {
        struct upipe_ts_mux_input *input =
            upipe_ts_mux_input_from_uchain(uchain);
        if (input->start_dts == UINT64_MAX)
            return; /* an input is not ready yet */
        if (input->start_dts < earliest_dts) {
            earliest_dts = input->start_dts;
            earliest_dts_sys = input->start_dts_sys;
        }
    }

    bool first = upipe_ts_mux_program->start_dts == UINT64_MAX;
    upipe_ts_mux_program->start_dts = earliest_dts - PMT_OFFSET;
    if (earliest_dts_sys != UINT64_MAX)
        upipe_ts_mux_program->start_dts_sys = earliest_dts_sys - PMT_OFFSET;

    struct upipe_ts_mux *upipe_ts_mux =
        upipe_ts_mux_from_program_mgr(upipe->mgr);
    if (first)
        upipe_ts_mux_start(upipe_ts_mux_to_upipe(upipe_ts_mux));

    /* Build a new PMT. */
    struct uref *uref = uref_alloc(upipe_ts_mux->uref_mgr);
    if (unlikely(uref == NULL ||
                 (first && !uref_clock_set_dts(uref, earliest_dts)) ||
                 (first && !uref_clock_set_vbv_delay(uref, PMT_OFFSET)) ||
                 (first && earliest_dts_sys != UINT64_MAX &&
                  !uref_clock_set_dts_sys(uref, earliest_dts_sys)))) {
        if (uref != NULL)
            uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    /* FIXME in case of deletion PMT will be output too early */
    upipe_input(upipe_ts_mux_program->program_psig, uref, NULL);
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static inline bool upipe_ts_mux_program_get_pmt_interval(struct upipe *upipe,
                                                         uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->pmt_interval;
    return true;
}

/** @internal @This sets the PMT interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return false in case of error
 */
static inline bool upipe_ts_mux_program_set_pmt_interval(struct upipe *upipe,
                                                         uint64_t interval)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    upipe_ts_mux_program->pmt_interval = interval;
    if (upipe_ts_mux_program->pmt_psii != NULL)
        upipe_ts_psii_sub_set_interval(upipe_ts_mux_program->pmt_psii,
                                       interval);
    upipe_ts_mux_program_update(upipe);
    return true;
}

/** @internal @This returns the current PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static inline bool upipe_ts_mux_program_get_pcr_interval(struct upipe *upipe,
                                                         uint64_t *interval_p)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux_program->pcr_interval;
    return true;
}

/** @internal @This sets the PCR interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return false in case of error
 */
static inline bool upipe_ts_mux_program_set_pcr_interval(struct upipe *upipe,
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
    return true;
}

/** @internal @This processes control commands on a ts_mux_program pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_mux_program_control(struct upipe *upipe,
                                         enum upipe_command command,
                                         va_list args)
{
    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_mux_program_get_sub_mgr(upipe, p);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_mux_program_get_super(upipe, p);
        }

        case UPIPE_TS_MUX_GET_PMT_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_pmt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PMT_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_pmt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PCR_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mux_program_get_pcr_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PCR_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mux_program_set_pcr_interval(upipe, interval);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_program_free(struct upipe *upipe)
{
    struct upipe_ts_mux_program *upipe_ts_mux_program =
        upipe_ts_mux_program_from_upipe(upipe);
    struct upipe_ts_mux *mux = upipe_ts_mux_from_program_mgr(upipe->mgr);

    if (upipe_ts_mux_program->program_psig != NULL)
        upipe_release(upipe_ts_mux_program->program_psig);
    if (upipe_ts_mux_program->pmt_psii != NULL)
        upipe_release(upipe_ts_mux_program->pmt_psii);
    upipe_throw_dead(upipe);

    upipe_ts_mux_program_clean_sub_inputs(upipe);
    upipe_ts_mux_program_clean_sub(upipe);
    upipe_ts_mux_program_free_flow(upipe);

    if (!upipe_single(upipe_ts_mux_to_upipe(mux)))
        upipe_ts_mux_change(upipe_ts_mux_to_upipe(mux));
    upipe_release(upipe_ts_mux_to_upipe(mux));
}

/** @internal @This initializes the program manager for a ts_mux pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_mux->program_mgr;
    program_mgr->signature = UPIPE_TS_MUX_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_mux_program_alloc;
    program_mgr->upipe_input = NULL;
    program_mgr->upipe_control = upipe_ts_mux_program_control;
    program_mgr->upipe_free = upipe_ts_mux_program_free;
    program_mgr->upipe_mgr_free = NULL;
}


/*
 * upipe_ts_mux structure handling (derived from upipe structure)
 */

/** @internal @This catches the need_uref_mgr and need_ubuf_mgr events from
 * psii subpipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param subpipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_mux_agg_probe(struct uprobe *uprobe,
                                         struct upipe *subpipe,
                                         enum uprobe_event event, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux =
        container_of(uprobe, struct upipe_ts_mux, agg_probe);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    switch (event) {
        case UPROBE_NEED_UREF_MGR:
            if (unlikely(upipe_ts_mux->uref_mgr == NULL))
                upipe_throw_need_uref_mgr(upipe);
            if (unlikely(upipe_ts_mux->uref_mgr != NULL))
                upipe_set_uref_mgr(subpipe, upipe_ts_mux->uref_mgr);
            return true;
        case UPROBE_NEED_UBUF_MGR: {
            struct uref *flow_def = va_arg(args, struct uref *);
            if (unlikely(upipe_ts_mux->ubuf_mgr == NULL))
                upipe_throw_need_ubuf_mgr(upipe, flow_def);
            if (unlikely(upipe_ts_mux->ubuf_mgr != NULL))
                upipe_set_ubuf_mgr(subpipe, upipe_ts_mux->ubuf_mgr);
            return true;
        }
        default:
            return false;
    }
}

/** @internal @This catches the need_uref_mgr and need_ubuf_mgr events from
 * subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_mux
 * @param subpipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_mux_probe(struct uprobe *uprobe, struct upipe *subpipe,
                               enum uprobe_event event, va_list args)
{
    struct upipe_ts_mux *upipe_ts_mux =
        container_of(uprobe, struct upipe_ts_mux, probe);
    struct upipe *upipe = upipe_ts_mux_to_upipe(upipe_ts_mux);

    switch (event) {
        case UPROBE_NEED_UREF_MGR:
            if (unlikely(upipe_ts_mux->uref_mgr == NULL))
                upipe_throw_need_uref_mgr(upipe);
            if (unlikely(upipe_ts_mux->uref_mgr != NULL))
                upipe_set_uref_mgr(subpipe, upipe_ts_mux->uref_mgr);
            return true;
        case UPROBE_NEED_UBUF_MGR: {
            struct uref *flow_def = va_arg(args, struct uref *);
            if (unlikely(upipe_ts_mux->ubuf_mgr == NULL))
                upipe_throw_need_ubuf_mgr(upipe, flow_def);
            if (unlikely(upipe_ts_mux->ubuf_mgr != NULL))
                upipe_set_ubuf_mgr(subpipe, upipe_ts_mux->ubuf_mgr);
            return true;
        }
        case UPROBE_NEW_FLOW_DEF:
            return true;
        default:
            return false;
    }
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
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_mux_alloc_flow(mgr, uprobe, signature, args,
                                                  &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    bool ret = true;
    uint64_t tsid = 0;
    if (!uref_ts_flow_get_tsid(flow_def, &tsid))
        ret = ret && uref_ts_flow_set_tsid(flow_def, DEFAULT_TSID);

    uint64_t octetrate;
    if (!uref_block_flow_get_octetrate(flow_def, &octetrate))
        ret = ret && uref_block_flow_set_octetrate(flow_def, TB_RATE_PSI);
    ret = ret && uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI);
    ret = ret && uref_ts_flow_set_pid(flow_def, 0);

    if (unlikely(!ret)) {
        uref_free(flow_def);
        upipe_ts_mux_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux_init_uref_mgr(upipe);
    upipe_ts_mux_init_ubuf_mgr(upipe);
    upipe_ts_mux_init_bin(upipe);
    upipe_ts_mux_init_program_mgr(upipe);
    upipe_ts_mux_init_sub_programs(upipe);
    upipe_ts_mux->psig = upipe_ts_mux->join = upipe_ts_mux->pat_psii = NULL;
    upipe_ts_mux->conformance = UPIPE_TS_CONFORMANCE_ISO;
    upipe_ts_mux->pat_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pmt_interval = DEFAULT_PSI_INTERVAL_ISO;
    upipe_ts_mux->pcr_interval = DEFAULT_PCR_INTERVAL;
    upipe_ts_mux->sid_auto = DEFAULT_SID_AUTO;
    upipe_ts_mux->pid_auto = DEFAULT_PID_AUTO;
    upipe_ts_mux->start_dts = UINT64_MAX;
    upipe_ts_mux->start_dts_sys = UINT64_MAX;
    upipe_ts_mux->padding_octetrate = 0;
    upipe_ts_mux->total_octetrate = (uint64_t)TS_SIZE *
        ((UCLOCK_FREQ + upipe_ts_mux->pat_interval - 1) /
         upipe_ts_mux->pat_interval);
    upipe_ts_mux->octetrate_auto = true;

    uprobe_init(&upipe_ts_mux->probe, upipe_ts_mux_probe, uprobe);
    uprobe_init(&upipe_ts_mux->agg_probe, upipe_ts_mux_agg_probe,
                &upipe_ts_mux->agg_probe_bin);

    upipe_throw_ready(upipe);

    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(upipe->mgr);
    upipe_ts_mux->psig =
        upipe_flow_alloc(ts_mux_mgr->ts_psig_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_mux->probe,
                                                UPROBE_LOG_DEBUG, "psig"),
                         flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_mux->psig == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_INVALID);
        return upipe;
    }

    upipe_throw_need_uref_mgr(upipe);
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
    struct uref *flow_def;

    upipe_ts_mux->join =
        upipe_void_alloc(ts_mux_mgr->ts_join_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_mux->probe,
                                                UPROBE_LOG_DEBUG, "join"));
    if (unlikely(upipe_ts_mux->join == NULL ||
                 !upipe_set_uref_mgr(upipe_ts_mux->join,
                                     upipe_ts_mux->uref_mgr) ||
                 !upipe_get_flow_def(upipe_ts_mux->join, &flow_def))) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    upipe_ts_mux->psii =
        upipe_flow_alloc(ts_mux_mgr->ts_psii_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_mux->probe,
                                                UPROBE_LOG_DEBUG, "psii"),
                         flow_def);
    if (unlikely(upipe_ts_mux->psii == NULL ||
                 !upipe_get_flow_def(upipe_ts_mux->psii, &flow_def))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    upipe_set_output(upipe_ts_mux->join, upipe_ts_mux->psii);

    struct upipe *agg =
        upipe_flow_alloc(ts_mux_mgr->ts_agg_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_mux->agg_probe,
                                                UPROBE_LOG_DEBUG, "agg"),
                         flow_def);
    if (unlikely(agg == NULL)) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    upipe_set_output(upipe_ts_mux->psii, agg);
    upipe_ts_mux_store_last_subpipe(upipe, agg);

    if (unlikely(!upipe_ts_mux_set_mode(upipe_ts_mux->agg,
                                        UPIPE_TS_MUX_MODE_CAPPED) ||
                 !upipe_get_flow_def(upipe_ts_mux->psig, &flow_def) ||
                 (upipe_ts_mux->pat_psii =
                  upipe_flow_alloc_sub(upipe_ts_mux->psii,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_mux->probe,
                                                UPROBE_LOG_DEBUG, "pat psii"),
                         flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    upipe_ts_psii_sub_set_interval(upipe_ts_mux->pat_psii,
                                   upipe_ts_mux->pat_interval);
    upipe_set_output(upipe_ts_mux->psig, upipe_ts_mux->pat_psii);
}

/** @This calculates the total octetrate used by a stream and updates the
 * aggregate subpipe.
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

    uint64_t earliest_dts = UINT64_MAX, earliest_dts_sys = UINT64_MAX;
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_mux->programs, uchain) {
        struct upipe_ts_mux_program *program =
            upipe_ts_mux_program_from_uchain(uchain);
        if (program->start_dts == UINT64_MAX)
            return; /* a program is not ready yet */
        if (program->start_dts < earliest_dts) {
            earliest_dts = program->start_dts;
            earliest_dts_sys = program->start_dts_sys;
        }
    }

    bool first = upipe_ts_mux->start_dts == UINT64_MAX;
    upipe_ts_mux->start_dts = earliest_dts;
    upipe_ts_mux->start_dts_sys = earliest_dts_sys;

    /* Build a new PAT. */
    struct uref *uref = uref_alloc(upipe_ts_mux->uref_mgr);
    if (unlikely(uref == NULL ||
                 (first && !uref_clock_set_dts(uref, earliest_dts)) ||
                 (first && !uref_clock_set_vbv_delay(uref, PAT_OFFSET)) ||
                 (first && earliest_dts_sys != UINT64_MAX &&
                  !uref_clock_set_dts_sys(uref, earliest_dts_sys)))) {
        if (uref != NULL)
            uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
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

/** @internal @This returns the current conformance mode. It cannot
 * return CONFORMANCE_AUTO.
 *
 * @param upipe description structure of the pipe
 * @param conformance_p filled in with the conformance
 * @return false in case of error
 */
static bool _upipe_ts_mux_get_conformance(struct upipe *upipe,
                                enum upipe_ts_conformance *conformance_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(conformance_p != NULL);
    *conformance_p = upipe_ts_mux->conformance;
    return true;
}

/** @internal @This sets the conformance mode.
 *
 * @param upipe description structure of the pipe
 * @param conformance conformance mode
 * @return false in case of error
 */
static bool _upipe_ts_mux_set_conformance(struct upipe *upipe,
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
            return false;
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
    return true;
}

/** @internal @This returns the current PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_get_pat_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pat_interval;
    return true;
}

/** @internal @This sets the PAT interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_set_pat_interval(struct upipe *upipe,
                                                  uint64_t interval)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->pat_interval = interval;
    if (upipe_ts_mux->pat_psii != NULL)
        upipe_ts_psii_sub_set_interval(upipe_ts_mux->pat_psii, interval);
    upipe_ts_mux_update(upipe);
    return true;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_get_pmt_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pmt_interval;
    return true;
}

/** @internal @This sets the PMT interval. It takes effect at the end of the
 * current period.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_set_pmt_interval(struct upipe *upipe,
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
    return true;
}

/** @internal @This returns the current PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_get_pcr_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_mux->pcr_interval;
    return true;
}

/** @internal @This sets the PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_set_pcr_interval(struct upipe *upipe,
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
    return true;
}

/** @internal @This returns the current padding octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_get_padding_octetrate(struct upipe *upipe,
                                                       uint64_t *octetrate_p)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    assert(octetrate_p != NULL);
    *octetrate_p = upipe_ts_mux->padding_octetrate;
    return true;
}

/** @internal @This sets the PCR octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_set_padding_octetrate(struct upipe *upipe,
                                                       uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    upipe_ts_mux->padding_octetrate = octetrate;
    upipe_ts_mux_update(upipe);
    return true;
}

/** @internal @This sets the mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return false in case of error
 */
static inline bool _upipe_ts_mux_set_octetrate(struct upipe *upipe,
                                               uint64_t octetrate)
{
    struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
    if (upipe_ts_mux->agg == NULL)
        return false;

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
 * @return false in case of error
 */
static bool upipe_ts_mux_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_ts_mux_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            if (!upipe_ts_mux_set_uref_mgr(upipe, uref_mgr))
                return false;
            /* To create the flow definition. */
            struct upipe_ts_mux *upipe_ts_mux = upipe_ts_mux_from_upipe(upipe);
            if (upipe_ts_mux->join == NULL)
                upipe_ts_mux_init(upipe);
            return true;
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_ts_mux_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_ts_mux_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_mux_get_sub_mgr(upipe, p);
        }

        case UPIPE_TS_MUX_GET_CONFORMANCE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            enum upipe_ts_conformance *conformance_p =
                va_arg(args, enum upipe_ts_conformance *);
            return _upipe_ts_mux_get_conformance(upipe, conformance_p);
        }
        case UPIPE_TS_MUX_SET_CONFORMANCE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            enum upipe_ts_conformance conformance =
                va_arg(args, enum upipe_ts_conformance);
            return _upipe_ts_mux_set_conformance(upipe, conformance);
        }
        case UPIPE_TS_MUX_GET_PAT_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_pat_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PAT_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_pat_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PMT_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_pmt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PMT_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_pmt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PCR_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_pcr_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PCR_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_pcr_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_PADDING_OCTETRATE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_mux_get_padding_octetrate(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PADDING_OCTETRATE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_mux_set_padding_octetrate(upipe, interval);
        }

        default:
            return upipe_ts_mux_control_bin(upipe, command, args);
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mux_free(struct upipe *upipe)
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
    upipe_throw_dead(upipe);
    upipe_ts_mux_clean_sub_programs(upipe);
    upipe_ts_mux_clean_bin(upipe);
    upipe_ts_mux_clean_ubuf_mgr(upipe);
    upipe_ts_mux_clean_uref_mgr(upipe);
    upipe_ts_mux_free_flow(upipe);
}

/** @This frees a upipe manager.
 *
 * @param mgr pointer to manager
 */
static void upipe_ts_mux_mgr_free(struct upipe_mgr *mgr)
{
    struct upipe_ts_mux_mgr *ts_mux_mgr =
        upipe_ts_mux_mgr_from_upipe_mgr(mgr);
    if (ts_mux_mgr->ts_join_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_join_mgr);
    if (ts_mux_mgr->ts_agg_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_agg_mgr);
    if (ts_mux_mgr->ts_encaps_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_encaps_mgr);
    if (ts_mux_mgr->ts_pese_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_pese_mgr);
    if (ts_mux_mgr->ts_psig_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_psig_mgr);
    if (ts_mux_mgr->ts_psii_mgr != NULL)
        upipe_mgr_release(ts_mux_mgr->ts_psii_mgr);

    urefcount_clean(&ts_mux_mgr->mgr.refcount);
    free(ts_mux_mgr);
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
    ts_mux_mgr->ts_pese_mgr = upipe_ts_pese_mgr_alloc();
    ts_mux_mgr->ts_psig_mgr = upipe_ts_psig_mgr_alloc();
    ts_mux_mgr->ts_psii_mgr = upipe_ts_psii_mgr_alloc();

    ts_mux_mgr->mgr.signature = UPIPE_TS_MUX_SIGNATURE;
    ts_mux_mgr->mgr.upipe_alloc = upipe_ts_mux_alloc;
    ts_mux_mgr->mgr.upipe_input = NULL;
    ts_mux_mgr->mgr.upipe_control = upipe_ts_mux_control;
    ts_mux_mgr->mgr.upipe_free = upipe_ts_mux_free;
    ts_mux_mgr->mgr.upipe_mgr_free = upipe_ts_mux_mgr_free;
    urefcount_init(&ts_mux_mgr->mgr.refcount);
    return upipe_ts_mux_mgr_to_upipe_mgr(ts_mux_mgr);
}

/** @This processes control commands on a ts_mux manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_mux_mgr_control_va(struct upipe_mgr *mgr,
                                 enum upipe_ts_mux_mgr_command command,
                                 va_list args)
{
    struct upipe_ts_mux_mgr *ts_mux_mgr = upipe_ts_mux_mgr_from_upipe_mgr(mgr);
    assert(urefcount_single(&ts_mux_mgr->mgr.refcount));

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_TS_MUX_MGR_GET_##NAME##_MGR: {                           \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = ts_mux_mgr->name##_mgr;                                    \
            return true;                                                    \
        }                                                                   \
        case UPIPE_TS_MUX_MGR_SET_##NAME##_MGR: {                           \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            if (ts_mux_mgr->name##_mgr != NULL)                             \
                upipe_mgr_release(ts_mux_mgr->name##_mgr);                  \
            if (m != NULL)                                                  \
                upipe_mgr_use(m);                                           \
            ts_mux_mgr->name##_mgr = m;                                     \
            return true;                                                    \
        }

        GET_SET_MGR(ts_join, TS_JOIN)
        GET_SET_MGR(ts_agg, TS_AGG)
        GET_SET_MGR(ts_encaps, TS_ENCAPS)
        GET_SET_MGR(ts_pese, TS_PESE)
        GET_SET_MGR(ts_psig, TS_PSIG)
        GET_SET_MGR(ts_psii, TS_PSII)
#undef GET_SET_MGR

        default:
            return false;
    }
}

/** @This processes control commands on a ts_mux manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_mux_mgr_control(struct upipe_mgr *mgr,
                              enum upipe_ts_mux_mgr_command command, ...)
{
    va_list args;
    va_start(args, command);
    bool ret = upipe_ts_mux_mgr_control_va(mgr, command, args);
    va_end(args);
    return ret;
}

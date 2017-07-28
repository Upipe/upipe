/*
 * Copyright (C) 2013-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module generating PSI tables
 * Normative references:
 *  - ISO/IEC 13818-1:2007(E) (MPEG-2 Systems)
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_psi_generator.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-framers/uref_mpga_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** T-STD TB octet rate for PSI tables */
#define TB_RATE_PSI 125000
/** default transport stream ID */
#define DEFAULT_TSID 65535
/** define to get timing verbosity */
#undef VERBOSE_TIMING

/** @internal @This is the private context of a ts psig pipe. */
struct upipe_ts_psig {
    /** refcount management structure */
    struct urefcount urefcount;
    /** input flow definition */
    struct uref *flow_def;
    /** true if the PAT update was frozen */
    bool frozen;
    /** cr_sys of the next preparation */
    uint64_t cr_sys_status;

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

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *output_flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** PAT version */
    uint8_t pat_version;
    /** PAT sections */
    struct uchain pat_sections;
    /** number of PAT sections */
    uint8_t pat_nb_sections;
    /** size of PAT sections */
    uint64_t pat_size;
    /** PAT interval */
    uint64_t pat_interval;
    /** PAT octetrate */
    uint64_t pat_octetrate;
    /** PAT last cr_sys */
    uint64_t pat_cr_sys;
    /** false if a new PAT was built but not sent yet */
    bool pat_sent;

    /** list of program subpipes */
    struct uchain programs;

    /** manager to create program subpipes */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig, upipe, UPIPE_TS_PSIG_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psig, urefcount, upipe_ts_psig_free)
UPIPE_HELPER_VOID(upipe_ts_psig)
UPIPE_HELPER_OUTPUT(upipe_ts_psig, output, output_flow_def, output_state,
                    request_list)
UPIPE_HELPER_UREF_MGR(upipe_ts_psig, uref_mgr, uref_mgr_request, NULL,
                      upipe_ts_psig_register_output_request,
                      upipe_ts_psig_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_ts_psig, ubuf_mgr, flow_format, ubuf_mgr_request,
                      NULL, upipe_ts_psig_register_output_request,
                      upipe_ts_psig_unregister_output_request)

/** @hidden */
static void upipe_ts_psig_build(struct upipe *upipe);
/** @hidden */
static void upipe_ts_psig_build_flow_def(struct upipe *upipe);
/** @hidden */
static void upipe_ts_psig_update_status(struct upipe *upipe);

/** @internal @This is the private context of a program of a ts_psig pipe. */
struct upipe_ts_psig_program {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** input flow definition */
    struct uref *flow_def;
    /** true if the PMT update was frozen */
    bool frozen;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *output_flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** PCR PID */
    uint16_t pcr_pid;
    /** PMT version */
    uint8_t pmt_version;
    /** PMT section */
    struct ubuf *pmt_section;
    /** size of PMT */
    uint64_t pmt_size;
    /** PMT interval */
    uint64_t pmt_interval;
    /** PMT octetrate */
    uint64_t pmt_octetrate;
    /** PMT last cr_sys */
    uint64_t pmt_cr_sys;
    /** false if a new PMT was built but not sent yet */
    bool pmt_sent;

    /** list of flow subpipes */
    struct uchain flows;

    /** manager to create flow subpipes */
    struct upipe_mgr flow_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig_program, upipe,
                   UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psig_program, urefcount,
                       upipe_ts_psig_program_free)
UPIPE_HELPER_VOID(upipe_ts_psig_program)
UPIPE_HELPER_OUTPUT(upipe_ts_psig_program, output, output_flow_def,
                    output_state, request_list)

UPIPE_HELPER_SUBPIPE(upipe_ts_psig, upipe_ts_psig_program, program, program_mgr,
                     programs, uchain)

/** @hidden */
static void upipe_ts_psig_program_build(struct upipe *upipe);
/** @hidden */
static void upipe_ts_psig_program_build_flow_def(struct upipe *upipe);

/** @internal @This is the private context of an elementary stream of a
 * ts_psig pipe. */
struct upipe_ts_psig_flow {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** input flow definition */
    struct uref *flow_def;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig_flow, upipe, UPIPE_TS_PSIG_FLOW_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psig_flow, urefcount, upipe_ts_psig_flow_free)
UPIPE_HELPER_VOID(upipe_ts_psig_flow)

UPIPE_HELPER_SUBPIPE(upipe_ts_psig_program, upipe_ts_psig_flow, flow, flow_mgr,
                     flows, uchain)

/** @internal @This allocates a flow of a ts_psig_program pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psig_flow_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct upipe *upipe = upipe_ts_psig_flow_alloc_void(mgr, uprobe, signature,
                                                        args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psig_flow *upipe_ts_psig_flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    upipe_ts_psig_flow_init_urefcount(upipe);
    upipe_ts_psig_flow->flow_def = NULL;
    upipe_ts_psig_flow_init_sub(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks if the flow is ready, and calculates the size
 * of the descriptors.
 *
 * @param upipe description structure of the pipe
 * @param descriptors_size_p filled in with the descriptors size
 * @return an error code
 */
static int upipe_ts_psig_flow_check(struct upipe *upipe,
                                    size_t *descriptors_size_p)
{
    struct upipe_ts_psig_flow *flow = upipe_ts_psig_flow_from_upipe(upipe);
    const char *raw_def;
    uint64_t pid;
    if (flow->flow_def == NULL ||
        !ubase_check(uref_ts_flow_get_pid(flow->flow_def, &pid)) ||
        !ubase_check(uref_flow_get_raw_def(flow->flow_def, &raw_def)))
        return UBASE_ERR_UNHANDLED;

    uint8_t languages = 0;
    uref_flow_get_languages(flow->flow_def, &languages);

    *descriptors_size_p = uref_ts_flow_size_descriptors(flow->flow_def);
    if (!ubase_ncmp(raw_def, "void.scte35."))
        *descriptors_size_p += DESC05_HEADER_SIZE;
    else if (!ubase_ncmp(raw_def, "block.dvb_teletext."))
        *descriptors_size_p += DESC56_HEADER_SIZE +
                               languages * DESC56_LANGUAGE_SIZE;
    else if (!ubase_ncmp(raw_def, "block.dvb_subtitle."))
        *descriptors_size_p += DESC59_HEADER_SIZE +
                               languages * DESC59_LANGUAGE_SIZE;

    else if (strstr(raw_def, ".sound.") != NULL) {
        if (languages)
            *descriptors_size_p += DESC0A_HEADER_SIZE +
                                   languages * DESC0A_LANGUAGE_SIZE;

        if (!ubase_ncmp(raw_def, "block.ac3.")) {
            *descriptors_size_p += DESC6A_HEADER_SIZE;
            uint8_t component_type;
            if (ubase_check(uref_ts_flow_get_component_type(flow->flow_def,
                                                            &component_type)))
                *descriptors_size_p += 1;
        } else if (!ubase_ncmp(raw_def, "block.eac3.")) {
            *descriptors_size_p += DESC7A_HEADER_SIZE;
            uint8_t component_type;
            if (ubase_check(uref_ts_flow_get_component_type(flow->flow_def,
                                                            &component_type)))
                *descriptors_size_p += 1;
        } else if (!ubase_ncmp(raw_def, "block.dts."))
            *descriptors_size_p += DESC7B_HEADER_SIZE;
        else if (!ubase_ncmp(raw_def, "block.opus."))
            *descriptors_size_p += DESC05_HEADER_SIZE;
        else if (ubase_ncmp(raw_def, "block.mp2.") &&
                   ubase_ncmp(raw_def, "block.mp3.") &&
                   ubase_ncmp(raw_def, "block.aac.") &&
                   ubase_ncmp(raw_def, "block.aac_latm.")) {
            upipe_warn_va(upipe, "unknown flow definition \"%s\"", raw_def);
            return UBASE_ERR_UNHANDLED;
        }
    } else if (ubase_ncmp(raw_def, "block.mpeg1video.") &&
               ubase_ncmp(raw_def, "block.mpeg2video.") &&
               ubase_ncmp(raw_def, "block.mpeg4.") &&
               ubase_ncmp(raw_def, "block.h264.") &&
               ubase_ncmp(raw_def, "block.hevc.")) {
        upipe_warn_va(upipe, "unknown flow definition \"%s\"", raw_def);
        return UBASE_ERR_UNHANDLED;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This prepares the ES part of a PMT.
 *
 * @param upipe description structure of the pipe
 * @param es pointer to ES in PMT
 * @param descriptors_size size of the descriptors
 * @return an error code
 */
static int upipe_ts_psig_flow_build(struct upipe *upipe, uint8_t *es,
                                    size_t descriptors_size)
{
    struct upipe_ts_psig_flow *flow = upipe_ts_psig_flow_from_upipe(upipe);
    const char *raw_def;
    uint64_t pid;
    if (flow->flow_def == NULL ||
        !ubase_check(uref_ts_flow_get_pid(flow->flow_def, &pid)) ||
        !ubase_check(uref_flow_get_raw_def(flow->flow_def, &raw_def)))
        return UBASE_ERR_UNHANDLED;

    uint8_t languages = 0;
    uref_flow_get_languages(flow->flow_def, &languages);

    uint8_t stream_type = PMT_STREAMTYPE_PRIVATE_PES;
    if (!ubase_ncmp(raw_def, "void.scte35."))
        stream_type = PMT_STREAMTYPE_SCTE_35;
    else if (!ubase_ncmp(raw_def, "block.mpeg1video."))
        stream_type = PMT_STREAMTYPE_VIDEO_MPEG1;
    else if (!ubase_ncmp(raw_def, "block.mpeg2video."))
        stream_type = PMT_STREAMTYPE_VIDEO_MPEG2;
    else if (!ubase_ncmp(raw_def, "block.mpeg4."))
        stream_type = PMT_STREAMTYPE_VIDEO_MPEG4;
    else if (!ubase_ncmp(raw_def, "block.h264."))
        stream_type = PMT_STREAMTYPE_VIDEO_AVC;
    else if (!ubase_ncmp(raw_def, "block.hevc."))
        stream_type = PMT_STREAMTYPE_VIDEO_HEVC;
    else if (!ubase_ncmp(raw_def, "block.aac.") ||
             !ubase_ncmp(raw_def, "block.aac_latm.")) {
        uint8_t encaps = UREF_MPGA_ENCAPS_ADTS;
        uref_mpga_flow_get_encaps(flow->flow_def, &encaps);
        stream_type = encaps == UREF_MPGA_ENCAPS_LOAS ?
                     PMT_STREAMTYPE_AUDIO_LATM :
                     PMT_STREAMTYPE_AUDIO_ADTS;
    } else if (!ubase_ncmp(raw_def, "block.mp2.") ||
               !ubase_ncmp(raw_def, "block.mp3.")) {
        uint64_t rate;
        if (ubase_check(uref_sound_flow_get_rate(flow->flow_def, &rate)) &&
            rate >= 32000)
            stream_type = PMT_STREAMTYPE_AUDIO_MPEG1;
        else
            stream_type = PMT_STREAMTYPE_AUDIO_MPEG2;
    }

    upipe_notice_va(upipe,
            " * ES pid=%"PRIu64" streamtype=0x%"PRIx8" def=\"%s\"",
            pid, stream_type, raw_def);

    pmtn_init(es);
    pmtn_set_streamtype(es, stream_type);
    pmtn_set_pid(es, pid);
    uint8_t *descs = pmtn_get_descs(es);
    descs_set_length(descs, descriptors_size);

    uint64_t k = 0;
    uint8_t *desc;
    if (!ubase_ncmp(raw_def, "void.scte35.")) {
        desc = descs_get_desc(descs, k++);
        assert(desc != NULL);
        desc05_init(desc);
        const uint8_t id[4] = { 'C', 'U', 'E', 'I' };
        desc05_set_identifier(desc, id);

    } else if (!ubase_ncmp(raw_def, "block.dvb_teletext.")) {
        desc = descs_get_desc(descs, k++);
        assert(desc != NULL);
        desc56_init(desc);
        desc_set_length(desc, DESC56_HEADER_SIZE - DESC_HEADER_SIZE +
                              languages * DESC56_LANGUAGE_SIZE);
        for (uint8_t l = 0; l < languages; l++) {
            uint8_t *language = desc56_get_language(desc, l);
            assert(language != NULL);
            const char *lang = "unk";
            if (strlen(lang) < 3)
                lang = "unk";
            uint8_t type = DESC56_TELETEXTTYPE_INFORMATION;
            uint8_t magazine = 0;
            uint8_t page = 0;
            uref_flow_get_language(flow->flow_def, &lang, l);
            uref_ts_flow_get_telx_type(flow->flow_def, &type, l);
            uref_ts_flow_get_telx_magazine(flow->flow_def, &magazine, l);
            uref_ts_flow_get_telx_page(flow->flow_def, &page, l);

            desc56n_set_code(language, (const uint8_t *)lang);
            desc56n_set_teletexttype(language, type);
            desc56n_set_teletextmagazine(language, magazine);
            desc56n_set_teletextpage(language, page);
        }

    } else if (!ubase_ncmp(raw_def, "block.dvb_subtitle.")) {
        desc = descs_get_desc(descs, k++);
        assert(desc != NULL);
        desc59_init(desc);
        desc_set_length(desc, DESC59_HEADER_SIZE - DESC_HEADER_SIZE +
                              languages * DESC59_LANGUAGE_SIZE);
        for (uint8_t l = 0; l < languages; l++) {
            uint8_t *language = desc59_get_language(desc, l);
            assert(language != NULL);
            const char *lang = "unk";
            if (strlen(lang) < 3)
                lang = "unk";
            /* DVB-subtitles (normal) with no AR criticality */
            uint8_t type = 0x10;
            uint8_t composition = 0;
            uint8_t ancillary = 88;
            uref_flow_get_language(flow->flow_def, &lang, l);
            uref_ts_flow_get_sub_type(flow->flow_def, &type, l);
            uref_ts_flow_get_sub_composition(flow->flow_def,
                                             &composition, l);
            uref_ts_flow_get_sub_ancillary(flow->flow_def, &ancillary, l);

            desc59n_set_code(language, (const uint8_t *)lang);
            desc59n_set_subtitlingtype(language, type);
            desc59n_set_compositionpage(language, composition);
            desc59n_set_ancillarypage(language, ancillary);
        }

    } else if (strstr(raw_def, ".sound.") != NULL) {
        if (languages) {
            desc = descs_get_desc(descs, k++);
            assert(desc != NULL);
            desc0a_init(desc);
            desc_set_length(desc, DESC0A_HEADER_SIZE - DESC_HEADER_SIZE +
                                  languages * DESC0A_LANGUAGE_SIZE);
            for (uint8_t l = 0; l < languages; l++) {
                uint8_t *language = desc0a_get_language(desc, l);
                assert(language != NULL);
                const char *lang = "unk";
                if (strlen(lang) < 3)
                    lang = "unk";
                uint8_t type = DESC0A_TYPE_UNDEFINED;
                uref_flow_get_language(flow->flow_def, &lang, l);
                if (ubase_check(uref_flow_get_hearing_impaired(
                                flow->flow_def, l)))
                    type = DESC0A_TYPE_HEARING_IMP;
                else if (ubase_check(uref_flow_get_visual_impaired(
                                flow->flow_def, l)))
                    type = DESC0A_TYPE_VISUAL_IMP;
                else if (ubase_check(uref_flow_get_audio_clean(
                                flow->flow_def, l)))
                    type = DESC0A_TYPE_CLEAN;

                desc0an_set_code(language, (const uint8_t *)lang);
                desc0an_set_audiotype(language, type);
            }
        }

        if (!ubase_ncmp(raw_def, "block.ac3.")) {
            desc = descs_get_desc(descs, k++);
            desc6a_init(desc);
            desc6a_clear_flags(desc);

            uint8_t component_type;
            if (ubase_check(uref_ts_flow_get_component_type(flow->flow_def,
                                                            &component_type))) {
                desc6a_set_component_type_flag(desc, true);
                desc6a_set_component_type(desc, component_type);
            }
            desc6a_set_length(desc);

        } else if (!ubase_ncmp(raw_def, "block.eac3.")) {
            desc = descs_get_desc(descs, k++);
            desc7a_init(desc);
            desc7a_clear_flags(desc);

            uint8_t component_type;
            if (ubase_check(uref_ts_flow_get_component_type(flow->flow_def,
                                                            &component_type))) {
                desc7a_set_component_type_flag(desc, true);
                desc7a_set_component_type(desc, component_type);
            }
            desc7a_set_length(desc);

        } else if (!ubase_ncmp(raw_def, "block.dts.")) {
            desc = descs_get_desc(descs, k++);
            desc7b_init(desc);

            uint64_t rate = 48000;
            uref_sound_flow_get_rate(flow->flow_def, &rate);
            switch (rate) {
                case 8000: desc7b_set_sample_rate_code(desc, 1); break;
                case 16000: desc7b_set_sample_rate_code(desc, 2); break;
                case 32000: desc7b_set_sample_rate_code(desc, 3); break;
                case 64000: desc7b_set_sample_rate_code(desc, 4); break;
                case 128000: desc7b_set_sample_rate_code(desc, 5); break;
                case 11025: desc7b_set_sample_rate_code(desc, 6); break;
                case 22050: desc7b_set_sample_rate_code(desc, 7); break;
                case 44100: desc7b_set_sample_rate_code(desc, 8); break;
                case 88200: desc7b_set_sample_rate_code(desc, 9); break;
                case 176400: desc7b_set_sample_rate_code(desc, 10); break;
                case 12000: desc7b_set_sample_rate_code(desc, 11); break;
                case 24000: desc7b_set_sample_rate_code(desc, 12); break;
                default:
                    upipe_warn_va(upipe, "invalid DTS sample rate %"PRIu64,
                                  rate);
                    /* intended pass-through */
                case 48000: desc7b_set_sample_rate_code(desc, 13); break;
                case 96000: desc7b_set_sample_rate_code(desc, 14); break;
                case 192000: desc7b_set_sample_rate_code(desc, 15); break;
            }

            uint64_t octetrate = 0;
            uref_block_flow_get_octetrate(flow->flow_def, &octetrate);
            switch (octetrate) {
                case 128000 / 8: desc7b_set_bit_rate_code(desc, 5); break;
                case 192000 / 8: desc7b_set_bit_rate_code(desc, 6); break;
                case 224000 / 8: desc7b_set_bit_rate_code(desc, 7); break;
                case 256000 / 8: desc7b_set_bit_rate_code(desc, 8); break;
                case 320000 / 8: desc7b_set_bit_rate_code(desc, 9); break;
                case 384000 / 8: desc7b_set_bit_rate_code(desc, 10); break;
                case 448000 / 8: desc7b_set_bit_rate_code(desc, 11); break;
                case 512000 / 8: desc7b_set_bit_rate_code(desc, 12); break;
                case 576000 / 8: desc7b_set_bit_rate_code(desc, 13); break;
                case 640000 / 8: desc7b_set_bit_rate_code(desc, 14); break;
                case 768000 / 8: desc7b_set_bit_rate_code(desc, 15); break;
                case 960000 / 8: desc7b_set_bit_rate_code(desc, 16); break;
                case 1024000 / 8: desc7b_set_bit_rate_code(desc, 17); break;
                case 1152000 / 8: desc7b_set_bit_rate_code(desc, 18); break;
                case 1280000 / 8: desc7b_set_bit_rate_code(desc, 19); break;
                case 1344000 / 8: desc7b_set_bit_rate_code(desc, 20); break;
                case 1408000 / 8: desc7b_set_bit_rate_code(desc, 21); break;
                case 1411200 / 8: desc7b_set_bit_rate_code(desc, 22); break;
                case 1472000 / 8: desc7b_set_bit_rate_code(desc, 23); break;
                case 1536000 / 8: desc7b_set_bit_rate_code(desc, 24); break;
                case 1920000 / 8: desc7b_set_bit_rate_code(desc, 25); break;
                case 2048000 / 8: desc7b_set_bit_rate_code(desc, 26); break;
                case 3072000 / 8: desc7b_set_bit_rate_code(desc, 27); break;
                case 3840000 / 8: desc7b_set_bit_rate_code(desc, 28); break;
                default: desc7b_set_bit_rate_code(desc, 29); break;
            }

            /* FIXME */
            desc7b_set_nblks(desc, 1024 / 32 - 1);
            desc7b_set_fsize(desc, 8192);

            uint8_t channels = 0;
            uref_sound_flow_get_channels(flow->flow_def, &channels);
            desc7b_set_lfe_flag(desc, false);
            switch (channels) {
                case 1: desc7b_set_surround_mode(desc, 0); break;
                case 2: desc7b_set_surround_mode(desc, 1); break;
                case 3: desc7b_set_surround_mode(desc, 5); break;
                case 4: desc7b_set_surround_mode(desc, 8); break;
                case 5: desc7b_set_surround_mode(desc, 9); break;
                case 6:
                    desc7b_set_lfe_flag(desc, true);
                    desc7b_set_surround_mode(desc, 9);
                    break;
                default: desc7b_set_surround_mode(desc, 0x3f); break;
            }

            /* FIXME */
            desc7b_set_extended_surround_flag(desc, 0);

        } else if (!ubase_ncmp(raw_def, "block.opus.")) {
            desc = descs_get_desc(descs, k++);
            desc05_init(desc);
            desc05_set_identifier(desc, (const uint8_t *)"opus");
        }
    }

    uref_ts_flow_extract_descriptors(flow->flow_def, descs_get_desc(descs, k));
    return UBASE_ERR_NONE;
}

/** @internal @This compares two programs wrt. ascending PIDs.
 *
 * @param uchain1 pointer to first flow
 * @param uchain2 pointer to second flow
 * @return an integer less than, equal to, or greater than zero if the first
 * argument is considered to be respectively less than, equal to, or greater
 * than the second.
 */
static int upipe_ts_psig_flow_compare(struct uchain **uchain1,
                                      struct uchain **uchain2)
{
    struct upipe_ts_psig_flow *flow1 = upipe_ts_psig_flow_from_uchain(*uchain1);
    struct upipe_ts_psig_flow *flow2 = upipe_ts_psig_flow_from_uchain(*uchain2);
    if (flow1->flow_def == NULL && flow2->flow_def == NULL)
        return 0;
    if (flow1->flow_def == NULL)
        return -1;
    else if (flow2->flow_def == NULL)
        return 1;
    return uref_ts_flow_cmp_pid(flow1->flow_def, flow2->flow_def);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psig_flow_set_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uint64_t pid;
    const char *raw_def;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    UBASE_RETURN(uref_ts_flow_get_pid(flow_def, &pid))
    UBASE_RETURN(uref_flow_get_raw_def(flow_def, &raw_def))

    struct upipe_ts_psig_flow *flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    bool pmt_change = flow->flow_def == NULL ||
        uref_flow_cmp_raw_def(flow_def, flow->flow_def) ||
        uref_ts_flow_cmp_pid(flow_def, flow->flow_def) ||
        uref_ts_flow_compare_descriptors(flow_def, flow->flow_def) ||
        uref_ts_flow_cmp_component_type(flow_def, flow->flow_def);

    uref_free(flow->flow_def);
    flow->flow_def = flow_def_dup;

    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
    ulist_sort(&program->flows, upipe_ts_psig_flow_compare);

    if (pmt_change) {
        upipe_ts_psig_program_build(upipe_ts_psig_program_to_upipe(program));
        upipe_ts_psig_program_build_flow_def(upipe_ts_psig_program_to_upipe(program));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_psig_flow_control(struct upipe *upipe,
                                      int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe_ts_psig_program_to_upipe(upipe_ts_psig_program)->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_alloc_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe_ts_psig_program_to_upipe(upipe_ts_psig_program)->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_free_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psig_flow_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_flow_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_flow_free(struct upipe *upipe)
{
    struct upipe_ts_psig_flow *upipe_ts_psig_flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
    upipe_throw_dead(upipe);
    upipe_use(upipe_ts_psig_program_to_upipe(program));

    uref_free(upipe_ts_psig_flow->flow_def);
    upipe_ts_psig_flow_clean_sub(upipe);

    upipe_ts_psig_flow_clean_urefcount(upipe);
    upipe_ts_psig_flow_free_void(upipe);

    upipe_ts_psig_program_build(upipe_ts_psig_program_to_upipe(program));
    upipe_ts_psig_program_build_flow_def(upipe_ts_psig_program_to_upipe(program));
    upipe_release(upipe_ts_psig_program_to_upipe(program));
}

/** @internal @This initializes the flow manager for a ts_psig_program pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_init_flow_mgr(struct upipe *upipe)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_mgr *flow_mgr = &upipe_ts_psig_program->flow_mgr;
    flow_mgr->refcount =
        upipe_ts_psig_program_to_urefcount(upipe_ts_psig_program);
    flow_mgr->signature = UPIPE_TS_PSIG_FLOW_SIGNATURE;
    flow_mgr->upipe_alloc = upipe_ts_psig_flow_alloc;
    flow_mgr->upipe_input = NULL;
    flow_mgr->upipe_control = upipe_ts_psig_flow_control;
    flow_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a program of a ts_psig pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psig_program_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct upipe *upipe = upipe_ts_psig_program_alloc_void(mgr, uprobe,
                                                           signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program_init_urefcount(upipe);
    upipe_ts_psig_program_init_output(upipe);
    upipe_ts_psig_program_init_flow_mgr(upipe);
    upipe_ts_psig_program_init_sub_flows(upipe);
    upipe_ts_psig_program_init_sub(upipe);
    upipe_ts_psig_program->flow_def = NULL;
    upipe_ts_psig_program->frozen = false;

    upipe_ts_psig_program->pcr_pid = 8191;
    upipe_ts_psig_program->pmt_version = 0;
    upipe_ts_psig_program->pmt_section = NULL;
    upipe_ts_psig_program->pmt_size = 0;
    upipe_ts_psig_program->pmt_interval = 0;
    upipe_ts_psig_program->pmt_octetrate = 0;
    upipe_ts_psig_program->pmt_cr_sys = 0;
    upipe_ts_psig_program->pmt_sent = false;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This builds a new output flow definition for PMT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    uint64_t pid;
    if (unlikely(!program->pmt_interval || !program->pmt_size ||
                 program->flow_def == NULL ||
                 !ubase_check(uref_ts_flow_get_pid(program->flow_def, &pid)))) {
        program->pmt_octetrate = 0;
        upipe_ts_psig_program_store_flow_def(upipe, NULL);
        return;
    }

    program->pmt_octetrate = (uint64_t)program->pmt_size * UCLOCK_FREQ /
                             program->pmt_interval;
    if (!program->pmt_octetrate)
        program->pmt_octetrate = 1;
    struct uref *flow_def = uref_alloc_control(psig->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtspmt.")) ||
        !ubase_check(uref_ts_flow_set_pid(flow_def, pid)) ||
        !ubase_check(uref_block_flow_set_size(flow_def, program->pmt_size)) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                program->pmt_interval)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def,
                program->pmt_octetrate)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_psig_program_store_flow_def(upipe, flow_def);
}

/** @internal @This generates a new PMT PSI section.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_build(struct upipe *upipe)
{
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    uint64_t program_number, pid;
    if (unlikely(program->flow_def == NULL ||
                 !ubase_check(uref_flow_get_id(program->flow_def,
                                               &program_number)) ||
                 !ubase_check(uref_ts_flow_get_pid(program->flow_def, &pid))))
        return;
    if (unlikely(psig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding a PMT");
        return;
    }

    size_t descriptors_size = uref_ts_flow_size_descriptors(program->flow_def);

    upipe_notice_va(upipe,
                    "new PMT program=%"PRIu16" version=%"PRIu8" pcrpid=%"PRIu16,
                    program_number, program->pmt_version, program->pcr_pid);

    struct ubuf *ubuf = ubuf_block_alloc(psig->ubuf_mgr,
                                         PSI_MAX_SIZE + PSI_HEADER_SIZE);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int size = -1;
    if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    pmt_init(buffer);
    /* set length later */
    psi_set_length(buffer, PSI_MAX_SIZE);
    pmt_set_program(buffer, program_number);
    psi_set_version(buffer, program->pmt_version);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, program->pcr_pid);
    uint8_t *descs = pmt_get_descs(buffer);
    descs_set_length(descs, descriptors_size);
    if (descriptors_size)
        uref_ts_flow_extract_descriptors(program->flow_def,
                descs_get_desc(descs, 0));

    uint16_t j = 0;
    struct uchain *uchain;
    ulist_foreach (&program->flows, uchain) {
        struct upipe_ts_psig_flow *flow =
            upipe_ts_psig_flow_from_uchain(uchain);
        size_t descriptors_size;
        if (!ubase_check(upipe_ts_psig_flow_check(
                        upipe_ts_psig_flow_to_upipe(flow), &descriptors_size)))
            continue;

        uint8_t *es = pmt_get_es(buffer, j);
        if (unlikely(es == NULL ||
                     !pmt_validate_es(buffer, es, descriptors_size))) {
            upipe_warn(upipe, "PMT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        upipe_ts_psig_flow_build(
                        upipe_ts_psig_flow_to_upipe(flow), es,
                        descriptors_size);
        j++;
    }

    uint8_t *es = pmt_get_es(buffer, j);
    pmt_set_length(buffer, es - buffer - PMT_HEADER_SIZE);
    uint16_t pmt_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
    psi_set_crc(buffer);
    ubuf_block_unmap(ubuf, 0);
    ubuf_block_resize(ubuf, 0, pmt_size);

    ubuf_free(program->pmt_section);
    program->pmt_section = ubuf;
    program->pmt_size = pmt_size;
    program->pmt_cr_sys = 0;
    program->pmt_sent = false;
    upipe_notice(upipe, "end PMT");
    upipe_ts_psig_update_status(upipe_ts_psig_to_upipe(psig));
}

/** @internal @This sends a PMT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys cr_sys of the next muxed packet
 */
static void upipe_ts_psig_program_send(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    if (unlikely(program->pmt_section == NULL || !program->pmt_interval ||
                 program->pmt_cr_sys + program->pmt_interval > cr_sys))
        return;

    program->pmt_cr_sys = cr_sys;
    if (!program->pmt_sent) {
        program->pmt_version++,
        program->pmt_version &= 0x1f;
        program->pmt_sent = true;
    }
    upipe_verbose_va(upipe, "sending PMT (%"PRIu64")", cr_sys);

    struct uref *uref = uref_alloc(psig->uref_mgr);
    struct ubuf *ubuf = ubuf_dup(program->pmt_section);
    if (unlikely(uref == NULL || ubuf == NULL)) {
        uref_free(uref);
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_attach_ubuf(uref, ubuf);
    uref_block_set_start(uref);
    uref_block_set_end(uref);
    uref_clock_set_cr_sys(uref, cr_sys);
    upipe_ts_psig_program_output(upipe, uref, NULL);
}

/** @internal @This compares two programs wrt. ascending program numbers.
 *
 * @param uchain1 pointer to first program
 * @param uchain2 pointer to second program
 * @return an integer less than, equal to, or greater than zero if the first
 * argument is considered to be respectively less than, equal to, or greater
 * than the second.
 */
static int upipe_ts_psig_program_compare(struct uchain **uchain1,
                                         struct uchain **uchain2)
{
    struct upipe_ts_psig_program *program1 =
        upipe_ts_psig_program_from_uchain(*uchain1);
    struct upipe_ts_psig_program *program2 =
        upipe_ts_psig_program_from_uchain(*uchain2);
    if (program1->flow_def == NULL && program2->flow_def == NULL)
        return 0;
    if (program1->flow_def == NULL)
        return -1;
    else if (program2->flow_def == NULL)
        return 1;
    return uref_flow_cmp_id(program1->flow_def, program2->flow_def);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psig_program_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uint64_t program_number, pid;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    UBASE_RETURN(uref_flow_get_id(flow_def, &program_number))
    UBASE_RETURN(uref_ts_flow_get_pid(flow_def, &pid))

    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    bool pmt_change = program->flow_def == NULL ||
        uref_flow_cmp_id(flow_def, program->flow_def) ||
        uref_ts_flow_cmp_pid(flow_def, program->flow_def) ||
        uref_ts_flow_compare_descriptors(flow_def, program->flow_def);

    bool pat_change = program->flow_def == NULL ||
        uref_flow_cmp_id(flow_def, program->flow_def) ||
        uref_ts_flow_cmp_pid(flow_def, program->flow_def);

    uref_free(program->flow_def);
    program->flow_def = flow_def_dup;

    struct upipe_ts_psig *psig = upipe_ts_psig_from_program_mgr(upipe->mgr);
    ulist_sort(&psig->programs, upipe_ts_psig_program_compare);

    if (pat_change) {
        upipe_ts_psig_build(upipe_ts_psig_to_upipe(psig));
        upipe_ts_psig_build_flow_def(upipe_ts_psig_to_upipe(psig));
    }

    if (pmt_change) {
        upipe_ts_psig_program_build(upipe);
        upipe_ts_psig_program_build_flow_def(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid_p filled in with the pcr_pid
 * @return an error code
 */
static int _upipe_ts_psig_program_get_pcr_pid(struct upipe *upipe,
                                              unsigned int *pcr_pid_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(pcr_pid_p != NULL);
    *pcr_pid_p = upipe_ts_psig_program->pcr_pid;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid pcr_pid
 * @return an error code
 */
static int _upipe_ts_psig_program_set_pcr_pid(struct upipe *upipe,
                                              unsigned int pcr_pid)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program->pcr_pid = pcr_pid;
    upipe_ts_psig_program_build(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_psig_program_get_pmt_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_psig_program->pmt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_psig_program_set_pmt_interval(struct upipe *upipe,
                                                  uint64_t interval)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *upipe_ts_psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    upipe_ts_psig_program->pmt_interval = interval;
    upipe_ts_psig_program_build_flow_def(upipe);
    upipe_ts_psig_update_status(upipe_ts_psig_to_upipe(upipe_ts_psig));
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current version.
 *
 * @param upipe description structure of the pipe
 * @param version_p filled in with the version
 * @return an error code
 */
static int upipe_ts_psig_program_get_version(struct upipe *upipe,
                                             unsigned int *version_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(version_p != NULL);
    *version_p = upipe_ts_psig_program->pmt_version;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the version.
 *
 * @param upipe description structure of the pipe
 * @param version version
 * @return an error code
 */
static int upipe_ts_psig_program_set_version(struct upipe *upipe,
                                             unsigned int version)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_dbg_va(upipe, "setting version to %u", version);
    upipe_ts_psig_program->pmt_version = version;
    upipe_ts_psig_program->pmt_version &= 0x1f;
    upipe_ts_psig_program_build(upipe);
    upipe_ts_psig_program_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_psig_program_control(struct upipe *upipe,
                                         int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_psig_program_control_flows(upipe, command, args));

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_alloc_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_free_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psig_program_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_ts_psig_program_control_output(upipe, command, args);
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_program_get_super(upipe, p);
        }

        case UPIPE_TS_PSIG_PROGRAM_GET_PCR_PID: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
            unsigned int *pcr_pid_p = va_arg(args, unsigned int *);
            return _upipe_ts_psig_program_get_pcr_pid(upipe, pcr_pid_p);
        }
        case UPIPE_TS_PSIG_PROGRAM_SET_PCR_PID: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
            unsigned int pcr_pid = va_arg(args, unsigned int);
            return _upipe_ts_psig_program_set_pcr_pid(upipe, pcr_pid);
        }
        case UPIPE_TS_MUX_GET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_psig_program_get_pmt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_psig_program_set_pmt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int *version_p = va_arg(args, unsigned int *);
            return upipe_ts_psig_program_get_version(upipe, version_p);
        }
        case UPIPE_TS_MUX_SET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int version = va_arg(args, unsigned int);
            return upipe_ts_psig_program_set_version(upipe, version);
        }
        case UPIPE_TS_MUX_FREEZE_PSI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_upipe(upipe);
            upipe_ts_psig_program->frozen = true;
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_free(struct upipe *upipe)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig = upipe_ts_psig_from_program_mgr(upipe->mgr);
    upipe_throw_dead(upipe);
    upipe_use(upipe_ts_psig_to_upipe(psig));

    upipe_ts_psig_program_clean_sub_flows(upipe);
    upipe_ts_psig_program_clean_sub(upipe);
    upipe_ts_psig_program_clean_output(upipe);
    uref_free(upipe_ts_psig_program->flow_def);
    ubuf_free(upipe_ts_psig_program->pmt_section);

    upipe_ts_psig_program_clean_urefcount(upipe);
    upipe_ts_psig_program_free_void(upipe);

    upipe_ts_psig_build(upipe_ts_psig_to_upipe(psig));
    upipe_ts_psig_build_flow_def(upipe_ts_psig_to_upipe(psig));
    upipe_release(upipe_ts_psig_to_upipe(psig));
}

/** @internal @This initializes the program manager for a ts_psig pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_psig->program_mgr;
    program_mgr->refcount = upipe_ts_psig_to_urefcount(upipe_ts_psig);
    program_mgr->signature = UPIPE_TS_PSIG_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_psig_program_alloc;
    program_mgr->upipe_input = NULL;
    program_mgr->upipe_control = upipe_ts_psig_program_control;
}

/** @internal @This allocates a ts_psig pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psig_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_psig_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_ts_psig_init_urefcount(upipe);
    upipe_ts_psig_init_uref_mgr(upipe);
    upipe_ts_psig_init_ubuf_mgr(upipe);
    upipe_ts_psig_init_output(upipe);
    upipe_ts_psig_init_program_mgr(upipe);
    upipe_ts_psig_init_sub_programs(upipe);
    upipe_ts_psig->flow_def = NULL;
    upipe_ts_psig->frozen = false;
    upipe_ts_psig->cr_sys_status = UINT64_MAX;

    upipe_ts_psig->pat_version = 0;
    ulist_init(&upipe_ts_psig->pat_sections);
    upipe_ts_psig->pat_interval = 0;
    upipe_ts_psig->pat_size = 0;
    upipe_ts_psig->pat_octetrate = 0;
    upipe_ts_psig->pat_cr_sys = 0;
    upipe_ts_psig->pat_sent = false;

    upipe_throw_ready(upipe);
    upipe_ts_psig_demand_uref_mgr(upipe);
    if (likely(upipe_ts_psig->uref_mgr != NULL)) {
        struct uref *flow_format = uref_alloc_control(upipe_ts_psig->uref_mgr);
        uref_flow_set_def(flow_format, "block.mpegtspsi.");
        upipe_ts_psig_demand_ubuf_mgr(upipe, flow_format);
    }
    return upipe;
}

/** @internal @This builds a new output flow definition for PAT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_build_flow_def(struct upipe *upipe)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    if (unlikely(!psig->pat_interval || !psig->pat_size)) {
        psig->pat_octetrate = 0;
        upipe_ts_psig_store_flow_def(upipe, NULL);
        return;
    }

    psig->pat_octetrate = (uint64_t)psig->pat_size * UCLOCK_FREQ /
                          psig->pat_interval;
    if (!psig->pat_octetrate)
        psig->pat_octetrate = 1;
    struct uref *flow_def = uref_alloc_control(psig->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtspat.")) ||
        !ubase_check(uref_ts_flow_set_pid(flow_def, PAT_PID)) ||
        !ubase_check(uref_block_flow_set_size(flow_def, psig->pat_size)) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                psig->pat_interval / psig->pat_nb_sections)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def,
                psig->pat_octetrate)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_psig_store_flow_def(upipe, flow_def);
}

/** @internal @This builds new PAT PSI sections.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_build(struct upipe *upipe)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    if (unlikely(psig->flow_def == NULL))
        return;
    if (unlikely(psig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding a PAT");
        return;
    }

    uint64_t tsid = DEFAULT_TSID;
    uref_flow_get_id(psig->flow_def, &tsid);

    upipe_notice_va(upipe, "new PAT tsid=%"PRIu64" version=%"PRIu8,
                    tsid, psig->pat_version);

    unsigned int nb_sections = 0;
    struct uchain *program_chain = &psig->programs;
    uint64_t total_size = 0;

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&psig->pat_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));

    do {
        if (unlikely(nb_sections >= PSI_TABLE_MAX_SECTIONS)) {
            upipe_warn(upipe, "PAT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        struct ubuf *ubuf = ubuf_block_alloc(psig->ubuf_mgr,
                                             PSI_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            break;
        }

        pat_init(buffer);
        /* set length later */
        psi_set_length(buffer, PSI_MAX_SIZE);
        pat_set_tsid(buffer, tsid);
        psi_set_version(buffer, psig->pat_version);
        psi_set_current(buffer);
        psi_set_section(buffer, nb_sections);
        /* set last section in the end */

        uint16_t j = 0;
        uint8_t *program;
        while ((program = pat_get_program(buffer, j)) != NULL &&
               !ulist_is_last(&psig->programs, program_chain)) {
            program_chain = program_chain->next;

            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_uchain(program_chain);
            uint64_t program_number, pmt_pid;
            if (upipe_ts_psig_program->flow_def == NULL ||
                !ubase_check(uref_flow_get_id(upipe_ts_psig_program->flow_def,
                            &program_number)) ||
                !ubase_check(uref_ts_flow_get_pid(upipe_ts_psig_program->flow_def,
                            &pmt_pid)) ||
                pmt_pid == 8192)
                continue;
            upipe_notice_va(upipe, " * program number=%"PRIu64" pid=%"PRIu64,
                            program_number, pmt_pid);

            j++;
            patn_init(program);
            patn_set_program(program, program_number);
            patn_set_pid(program, pmt_pid);
        }

        pat_set_length(buffer, program - buffer - PAT_HEADER_SIZE);
        uint16_t pat_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);

        ubuf_block_resize(ubuf, 0, pat_size);
        ulist_add(&psig->pat_sections, ubuf_to_uchain(ubuf));
        nb_sections++;
        total_size += pat_size;
    } while (!ulist_is_last(&psig->programs, program_chain));

    ulist_foreach (&psig->pat_sections, section_chain) {
        struct ubuf *ubuf = ubuf_from_uchain(section_chain);
        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        psi_set_lastsection(buffer, nb_sections - 1);
        psi_set_crc(buffer);

        ubuf_block_unmap(ubuf, 0);
    }

    psig->pat_cr_sys = 0;
    psig->pat_sent = false;

    upipe_notice_va(upipe, "end PAT (%u sections)", nb_sections);

    psig->pat_nb_sections = nb_sections;
    psig->pat_size = total_size;
    upipe_ts_psig_update_status(upipe);
}

/** @internal @This sends a PAT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys cr_sys of the next muxed packet
 */
static void upipe_ts_psig_send(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    if (unlikely(ulist_empty(&psig->pat_sections) || !psig->pat_interval ||
                 psig->pat_cr_sys + psig->pat_interval > cr_sys))
        return;

    psig->pat_cr_sys = cr_sys;
    if (!psig->pat_sent) {
        psig->pat_version++,
        psig->pat_version &= 0x1f;
        psig->pat_sent = true;
    }
    upipe_verbose_va(upipe, "sending PAT (%"PRIu64")", cr_sys);

    struct uchain *section_chain;
    ulist_foreach (&psig->pat_sections, section_chain) {
        bool last = ulist_is_last(&psig->pat_sections, section_chain);
        struct ubuf *ubuf = ubuf_dup(ubuf_from_uchain(section_chain));
        struct uref *uref = uref_alloc(psig->uref_mgr);
        size_t ubuf_size;
        if (unlikely(uref == NULL || ubuf == NULL ||
                     !ubase_check(ubuf_block_size(ubuf, &ubuf_size)))) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        uref_attach_ubuf(uref, ubuf);
        uref_block_set_start(uref);
        if (last)
            uref_block_set_end(uref);
        uref_clock_set_cr_sys(uref, cr_sys);
        upipe_ts_psig_output(upipe, uref, NULL);
        cr_sys += (uint64_t)ubuf_size * UCLOCK_FREQ / psig->pat_octetrate;
    }
}

/** @This updates the status to the mux.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_update_status(struct upipe *upipe)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    uint64_t cr_sys = UINT64_MAX;

    if (likely(psig->flow_def != NULL)) {
        if (likely(!ulist_empty(&psig->pat_sections) && psig->pat_interval))
            cr_sys = psig->pat_cr_sys + psig->pat_interval;

        struct uchain *uchain;
        ulist_foreach (&psig->programs, uchain) {
            struct upipe_ts_psig_program *program =
                upipe_ts_psig_program_from_uchain(uchain);
            if (likely(program->pmt_section != NULL && program->pmt_interval &&
                       cr_sys > program->pmt_cr_sys + program->pmt_interval))
                cr_sys = program->pmt_cr_sys + program->pmt_interval;
        }
    }

#ifdef VERBOSE_TIMING
    upipe_verbose_va(upipe, "status cr_sys=%"PRIu64, cr_sys);
#endif
    psig->cr_sys_status = cr_sys;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psig_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    bool pat_change = psig->flow_def == NULL ||
        uref_flow_cmp_id(flow_def, psig->flow_def);

    uref_free(psig->flow_def);
    psig->flow_def = flow_def_dup;

    if (pat_change) {
        upipe_ts_psig_build(upipe);
        upipe_ts_psig_build_flow_def(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_psig_get_pat_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_psig->pat_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_psig_set_pat_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_ts_psig->pat_interval = interval;
    upipe_ts_psig_build_flow_def(upipe);
    upipe_ts_psig_update_status(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current version.
 *
 * @param upipe description structure of the pipe
 * @param version_p filled in with the version
 * @return an error code
 */
static int upipe_ts_psig_get_version(struct upipe *upipe,
                                     unsigned int *version_p)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    assert(version_p != NULL);
    *version_p = upipe_ts_psig->pat_version;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the version.
 *
 * @param upipe description structure of the pipe
 * @param version version
 * @return an error code
 */
static int upipe_ts_psig_set_version(struct upipe *upipe,
                                     unsigned int version)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_dbg_va(upipe, "setting version to %u", version);
    upipe_ts_psig->pat_version = version;
    upipe_ts_psig->pat_version &= 0x1f;
    upipe_ts_psig_build(upipe);
    upipe_ts_psig_build_flow_def(upipe);
    return UBASE_ERR_NONE;
}

/** @This prepares the next PSI sections for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @param latency unused
 * @return an error code
 */
static int upipe_ts_psig_prepare(struct upipe *upipe, uint64_t cr_sys,
                                 uint64_t latency)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    if (likely(upipe_ts_psig->cr_sys_status > cr_sys))
        return UBASE_ERR_NONE;

    upipe_ts_psig_send(upipe, cr_sys);

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psig->programs, uchain) {
        struct upipe_ts_psig_program *program =
            upipe_ts_psig_program_from_uchain(uchain);
        upipe_ts_psig_program_send(upipe_ts_psig_program_to_upipe(program),
                                   cr_sys);
    }
    upipe_ts_psig_update_status(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_psig_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_psig_control_programs(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_ts_psig_control_output(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psig_set_flow_def(upipe, flow_def);
        }

        case UPIPE_TS_MUX_GET_PAT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_psig_get_pat_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PAT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_psig_set_pat_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int *version_p = va_arg(args, unsigned int *);
            return upipe_ts_psig_get_version(upipe, version_p);
        }
        case UPIPE_TS_MUX_SET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int version = va_arg(args, unsigned int);
            return upipe_ts_psig_set_version(upipe, version);
        }
        case UPIPE_TS_MUX_FREEZE_PSI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
            psig->frozen = true;
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_PREPARE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t cr_sys = va_arg(args, uint64_t);
            uint64_t latency = va_arg(args, uint64_t);
            return upipe_ts_psig_prepare(upipe, cr_sys, latency);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_free(struct upipe *upipe)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    upipe_throw_dead(upipe);

    uref_free(psig->flow_def);
    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&psig->pat_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));
    upipe_ts_psig_clean_sub_programs(upipe);
    upipe_ts_psig_clean_output(upipe);
    upipe_ts_psig_clean_ubuf_mgr(upipe);
    upipe_ts_psig_clean_uref_mgr(upipe);
    upipe_ts_psig_clean_urefcount(upipe);
    upipe_ts_psig_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_psig_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PSIG_SIGNATURE,

    .upipe_alloc = upipe_ts_psig_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_ts_psig_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_psig pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psig_mgr_alloc(void)
{
    return &upipe_ts_psig_mgr;
}

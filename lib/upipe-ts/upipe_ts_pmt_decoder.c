/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module decoding the program map table of TS streams
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/mpeg/psi/desc_0a.h>
#include <bitstream/dvb/si/desc_56.h>
#include <bitstream/dvb/si/desc_59.h>
#include <bitstream/dvb/si/desc_6a.h>
#include <bitstream/dvb/si/desc_7a.h>
#include <bitstream/dvb/si/desc_7b.h>
#include <bitstream/dvb/si/desc_7c.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtspmt."

/** @internal @This is the private context of a ts_pmtd pipe. */
struct upipe_ts_pmtd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** input flow definition */
    struct uref *flow_def_input;
    /** currently in effect PMT table */
    struct uref *pmt;
    /** list of flows */
    struct uchain flows;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pmtd, upipe, UPIPE_TS_PMTD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_pmtd, urefcount, upipe_ts_pmtd_free)
UPIPE_HELPER_VOID(upipe_ts_pmtd)
UPIPE_HELPER_OUTPUT(upipe_ts_pmtd, output, flow_def, output_state, request_list)

/** @internal @This allocates a ts_pmtd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pmtd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_pmtd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    upipe_ts_pmtd_init_urefcount(upipe);
    upipe_ts_pmtd_init_output(upipe);
    upipe_ts_pmtd->flow_def_input = NULL;
    upipe_ts_pmtd->pmt = NULL;
    ulist_init(&upipe_ts_pmtd->flows);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This cleans up the list of programs.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pmtd_clean_flows(struct upipe *upipe)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_pmtd->flows, uchain, uchain_tmp) {
        struct uref *flow_def = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(flow_def);
    }
}

/** @internal @This reads the header of a PMT.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param header pointer to PMT header
 * @param header_desc pointer to PMT descriptors
 * @param header_desclength size of PMT descriptors
 */
#define UPIPE_TS_PMTD_HEADER(upipe, pmt, header, header_desc,               \
                             header_desclength)                             \
    uint8_t header_buffer[PMT_HEADER_SIZE];                                 \
    const uint8_t *header = uref_block_peek(pmt, 0, PMT_HEADER_SIZE,        \
                                            header_buffer);                 \
    const uint8_t *header_desc = NULL;                                      \
    uint16_t header_desclength = likely(header != NULL) ?                   \
                                 pmt_get_desclength(header) : 0;            \
    uint8_t header_desc_buffer[header_desclength + 1];                      \
    if (header_desclength) {                                                \
        header_desc = uref_block_peek(pmt, PMT_HEADER_SIZE,                 \
                              header_desclength, header_desc_buffer);       \
        if (unlikely(header_desc == NULL)) {                                \
            uref_block_peek_unmap(pmt, 0, header_buffer, header);           \
            header = NULL;                                                  \
        }                                                                   \
    }                                                                       \

/** @internal @This unmaps the header of a PMT.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param header pointer to PMT header
 * @param header_desc pointer to PMT descriptors
 * @param header_desclength size of PMT descriptors
 */
#define UPIPE_TS_PMTD_HEADER_UNMAP(upipe, pmt, header, header_desc,         \
                                   header_desclength)                       \
    uref_block_peek_unmap(pmt, 0, header_buffer, header);                   \
    if (header_desclength)                                                  \
        uref_block_peek_unmap(pmt, PMT_HEADER_SIZE,                         \
                              header_desc_buffer, header_desc);

/** @internal @This walks through the elementary streams in a PMT.
 * This is the first part: read data from es afterwards.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param offset pointing to the current offset in uref
 * @param header_desclength size of PMT descriptors
 * @param es iterator pointing to ES definition
 * @param desc iterator pointing to descriptors of the ES
 * @param desclength pointing to size of ES descriptors
 */
#define UPIPE_TS_PMTD_PEEK(upipe, pmt, offset, header_desclength, es, desc, \
                           desclength)                                      \
    size_t size = 0;                                                        \
    uref_block_size(pmt, &size);                                            \
                                                                            \
    int offset = PMT_HEADER_SIZE + header_desclength;                       \
    while (offset + PMT_ES_SIZE <= size - PSI_CRC_SIZE) {                   \
        uint8_t es_buffer[PMT_ES_SIZE];                                     \
        const uint8_t *es = uref_block_peek(pmt, offset, PMT_ES_SIZE,       \
                                            es_buffer);                     \
        if (unlikely(es == NULL))                                           \
            break;                                                          \
        uint16_t desclength = pmtn_get_desclength(es);                      \
        /* + 1 is to avoid [0] */                                           \
        uint8_t desc_buffer[desclength + 1];                                \
        const uint8_t *desc;                                                \
        if (desclength) {                                                   \
            desc = uref_block_peek(pmt, offset + PMT_ES_SIZE,               \
                                   desclength, desc_buffer);                \
            if (unlikely(desc == NULL)) {                                   \
                uref_block_peek_unmap(pmt, offset, es_buffer, es);          \
                break;                                                      \
            }                                                               \
        } else                                                              \
            desc = NULL;

/** @internal @This walks through the elementary streams in a PMT.
 * This is the second part: do the actions afterwards.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param offset pointing to the current offset in uref
 * @param es iterator pointing to ES definition
 * @param desc iterator pointing to descriptors of the ES
 * @param desclength pointing to size of ES descriptors
 */
#define UPIPE_TS_PMTD_PEEK_UNMAP(upipe, pmt, offset, es, desc, desclength)  \
        uref_block_peek_unmap(pmt, offset, es_buffer, es);                  \
        if (desc != NULL)                                                   \
            uref_block_peek_unmap(pmt, offset + PMT_ES_SIZE,                \
                                        desc_buffer, desc);                 \
        offset += PMT_ES_SIZE + desclength;

/** @internal @This walks through the elementary streams in a PMT.
 * This is the last part.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param offset pointing to the current offset in uref
 */
#define UPIPE_TS_PMTD_PEEK_END(upipe, pmt, offset)                          \
    }                                                                       \
    if (unlikely(offset + PMT_ES_SIZE <= size - PSI_CRC_SIZE)) {            \
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);                          \
    }                                                                       \

/** @internal @This validates the next PMT.
 *
 * @param upipe description structure of the pipe
 * @param uref PMT section
 * @return false if the PMT is invalid
 */
static bool upipe_ts_pmtd_validate(struct upipe *upipe, struct uref *pmt)
{
    if (!upipe_ts_psid_check_crc(pmt))
        return false;

    UPIPE_TS_PMTD_HEADER(upipe, pmt, header, header_desc, header_desclength)

    if (unlikely(header == NULL))
        return false;

    bool validate = psi_get_syntax(header) && !psi_get_section(header) &&
                    !psi_get_lastsection(header) &&
                    psi_get_tableid(header) == PMT_TABLE_ID &&
                    (!header_desclength ||
                     descl_validate(header_desc, header_desclength));

    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, pmt, header, header_desc,
                               header_desclength)

    if (!validate)
        return false;
    
    UPIPE_TS_PMTD_PEEK(upipe, pmt, offset, header_desclength, es, desc,
                       desclength)

    validate = !desclength || descl_validate(desc, desclength);

    UPIPE_TS_PMTD_PEEK_UNMAP(upipe, pmt, offset, es, desc, desclength)

    if (!validate)
        return false;

    UPIPE_TS_PMTD_PEEK_END(upipe, pmt, offset)
    return true;
}

/** @internal @This compares a PMT header with the current PMT.
 *
 * @param upipe description structure of the pipe
 * @param header pointer to PMT header
 * @param header_desc pointer to PMT descriptors
 * @param header_desclength size of PMT descriptors
 * @return false if the headers are different
 */
static bool upipe_ts_pmtd_compare_header(struct upipe *upipe,
                                         const uint8_t *header,
                                         const uint8_t *header_desc,
                                         uint16_t header_desclength)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->pmt == NULL)
        return false;

    UPIPE_TS_PMTD_HEADER(upipe, upipe_ts_pmtd->pmt, old_header, old_header_desc,
                         old_header_desclength)

    if (unlikely(old_header == NULL))
        return false;

    bool compare = pmt_get_pcrpid(header) == pmt_get_pcrpid(old_header) &&
                   header_desclength == old_header_desclength &&
                   (!header_desclength ||
                    !memcmp(header_desc, old_header_desc, header_desclength));

    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, upipe_ts_pmtd->pmt, old_header,
                               old_header_desc, old_header_desclength)

    return compare;
}

/** @internal @This is a helper function to parse the stream type of the
 * elementary stream.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param streamtype MPEG stream type
 */
static void upipe_ts_pmtd_parse_streamtype(struct upipe *upipe,
                                           struct uref *flow_def,
                                           uint8_t streamtype)
{
    switch (streamtype) {
        case PMT_STREAMTYPE_VIDEO_MPEG1:
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                            "block.mpeg1video.pic."))
            UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                            "block.mpegts.mpegtspes.mpeg1video.pic."))
            break;

        case PMT_STREAMTYPE_VIDEO_MPEG2:
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                            "block.mpeg2video.pic."))
            UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                            "block.mpegts.mpegtspes.mpeg2video.pic."))
            break;

        case PMT_STREAMTYPE_AUDIO_MPEG1:
        case PMT_STREAMTYPE_AUDIO_MPEG2:
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.mp2.sound."))
            UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                            "block.mpegts.mpegtspes.mp2.sound."))
            break;

        case PMT_STREAMTYPE_AUDIO_ADTS:
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.aac.sound."))
            UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                            "block.mpegts.mpegtspes.aac.sound."))
            break;

        case PMT_STREAMTYPE_VIDEO_AVC:
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "block.h264.pic."))
            UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                            "block.mpegts.mpegtspes.h264.pic."))
            break;

        default:
            break;
    }
}

/** @internal @This is a helper function to parse descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the decriptor list
 * @return an error code
 */
static void upipe_ts_pmtd_parse_descs(struct upipe *upipe,
                                      struct uref *flow_def,
                                      const uint8_t *descl, uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool valid = false;
        bool copy = false;
        switch (desc_get_tag(desc)) {
            case 0x2: /* Video stream descriptor */
            case 0x3: /* Audio stream descriptor */
            case 0x4: /* Hierarchy descriptor */
            case 0x5: /* Registration descriptor */
                    if ((valid = desc05_validate(desc))) {
                        const uint8_t *identifier;
                        if ((identifier = desc05_get_identifier(desc))) {
                            if (!memcmp(identifier, "Opus", 4)){
                                UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                            "block.opus.sound."))
                                UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                            "block.mpegts.mpegtspes.opus.sound."))
                            }
                        }
                    }
            case 0x6: /* Data stream alignment descriptor */
                break;

            case 0xa: /* ISO 639 language descriptor */
                if ((valid = desc0a_validate(desc))) {
                    uint8_t j = 0;
                    uint8_t *language;
                    while ((language = desc0a_get_language((uint8_t *)desc,
                                                           j)) != NULL) {
                        char code[4];
                        memcpy(code, desc0an_get_code(language), 3);
                        code[3] = '\0';
                        UBASE_FATAL(upipe, uref_flow_set_language(flow_def,
                                                                  code, j))
                        switch (desc0an_get_audiotype(language)) {
                            case DESC0A_TYPE_HEARING_IMP:
                                UBASE_FATAL(upipe, uref_flow_set_hearing_impaired(flow_def, j))
                                break;
                            case DESC0A_TYPE_VISUAL_IMP:
                                UBASE_FATAL(upipe, uref_flow_set_visual_impaired(flow_def, j))
                                break;
                            default:
                                break;
                        }
                        j++;
                    }
                    UBASE_FATAL(upipe, uref_flow_set_languages(flow_def, j))
                }
                break;

            case 0xc: /* Multiplex buffer utilization descriptor */
            case 0xe: /* Maximum bitrate descriptor */
            case 0x10: /* Smoothing buffer descriptor */
            case 0x11: /* STD descriptor */
            case 0x12: /* IBP descriptor */
            case 0x1b: /* MPEG-4 video descriptor */
            case 0x1c: /* MPEG-4 audio descriptor */
            case 0x1d: /* IOD descriptor */
            case 0x1e: /* SL descriptor */
            case 0x1f: /* FMC descriptor */
            case 0x20: /* External_ES_ID descriptor */
            case 0x21: /* Muxcode descriptor */
            case 0x22: /* FmxBufferSize descriptor */
            case 0x23: /* MultiplexBuffer descriptor */
            case 0x27: /* Metadata STD descriptor */
                break;

            case 0x28: /* AVC video descriptor */
            case 0x2a: /* AVC timing and HRD descriptor */
            case 0x2b: /* MPEG-2 AAC descriptor */
            case 0x2c: /* FlexMuxTiming descriptor */
            case 0x2d: /* MPEG-4 test descriptor */
            case 0x2e: /* MPEG-4 audio extension descriptor */
            case 0x2f: /* Auxiliary video stream descriptor */
            case 0x30: /* SVC extension descriptor */
            case 0x31: /* MVC extension descriptor */
            case 0x32: /* J2K video descriptor */
            case 0x33: /* MVC operation point descriptor */
            case 0x34: /* MPEG2_stereoscopic_video_format descriptor */
            case 0x35: /* Stereoscopic_program_info descriptor */
            case 0x36: /* Stereoscopic_video:info descriptor */
                break;

            /* DVB */
            case 0x45: /* VBI data descriptor */
            case 0x46: /* VBI teletext descriptor */
            case 0x51: /* Mosaic descriptor */
            case 0x52: /* Stream identifier descriptor */
                break;

            case 0x56: /* Teletext descriptor */
                if ((valid = desc56_validate(desc))) {
                    UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                "block.dvb_teletext.pic.sub."))
                    UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.dvb_teletext.pic.sub."))

                    uint8_t j = 0;
                    uint8_t *language;
                    while ((language = desc56_get_language((uint8_t *)desc,
                                                           j)) != NULL) {
                        char code[4];
                        memcpy(code, desc56n_get_code(language), 3);
                        code[3] = '\0';
                        UBASE_FATAL(upipe, uref_flow_set_language(flow_def,
                                                                  code, j))
                        UBASE_FATAL(upipe, uref_ts_flow_set_telx_type(flow_def,
                                    desc56n_get_teletexttype(language), j))
                        UBASE_FATAL(upipe, uref_ts_flow_set_telx_magazine(flow_def,
                                    desc56n_get_teletextmagazine(language), j))
                        UBASE_FATAL(upipe, uref_ts_flow_set_telx_page(flow_def,
                                    desc56n_get_teletextpage(language), j))
                        j++;
                    }
                    UBASE_FATAL(upipe, uref_flow_set_languages(flow_def, j))
                }
                break;

            case 0x59: /* Subtitling descriptor */
                if ((valid = desc59_validate(desc))) {
                    UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                "block.dvb_subtitle.pic.sub."))
                    UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.dvb_subtitle.pic.sub."))

                    uint8_t j = 0;
                    uint8_t *language;
                    while ((language = desc59_get_language((uint8_t *)desc,
                                                           j)) != NULL) {
                        char code[4];
                        memcpy(code, desc59n_get_code(language), 3);
                        code[3] = '\0';
                        UBASE_FATAL(upipe, uref_flow_set_language(flow_def,
                                                                  code, j))
                        UBASE_FATAL(upipe, uref_ts_flow_set_sub_type(flow_def,
                                    desc59n_get_subtitlingtype(language), j))
                        UBASE_FATAL(upipe, uref_ts_flow_set_sub_composition(flow_def,
                                    desc59n_get_compositionpage(language), j))
                        UBASE_FATAL(upipe, uref_ts_flow_set_sub_ancillary(flow_def,
                                    desc59n_get_ancillarypage(language), j))
                        j++;
                    }
                    UBASE_FATAL(upipe, uref_flow_set_languages(flow_def, j))
                }
                break;

            case 0x6a: /* AC-3 descriptor */
                if ((valid = desc6a_validate(desc))) {
                    UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                "block.ac3.sound."))
                    UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.ac3.sound."))
                }
                break;

            case 0x6b: /* Ancillary data descriptor */
            case 0x70: /* Adaptation field data descriptor */
                break;

            case 0x7a: /* Enhanced AC-3 descriptor */
                if ((valid = desc7a_validate(desc))) {
                    UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                "block.eac3.sound."))
                    UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.eac3.sound."))
                }
                break;

            case 0x7b: /* DTS descriptor */
                if ((valid = desc7b_validate(desc))) {
                    UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                "block.dts.sound."))
                    UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.dts.sound."))
                }
                break;

            case 0x7c: /* AAC descriptor */
                if ((valid = desc7c_validate(desc))) {
                    UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                                "block.aac.sound."))
                    UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.aac.sound."))
                }
                break;

            default:
                copy = true;
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pmtd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    assert(upipe_ts_pmtd->flow_def_input != NULL);
    if (upipe_ts_pmtd->pmt != NULL &&
        ubase_check(uref_block_equal(upipe_ts_pmtd->pmt, uref))) {
        /* Identical PMT. */
        upipe_throw_new_rap(upipe, uref);
        uref_free(uref);
        return;
    }

    if (!upipe_ts_pmtd_validate(upipe, uref)) {
        upipe_warn(upipe, "invalid PMT section received");
        uref_free(uref);
        return;
    }
    upipe_throw_new_rap(upipe, uref);

    UPIPE_TS_PMTD_HEADER(upipe, uref, header, header_desc, header_desclength)

    if (unlikely(header == NULL)) {
        upipe_warn(upipe, "invalid PMT section received");
        uref_free(uref);
        return;
    }

    uint16_t pcrpid = pmt_get_pcrpid(header);
    bool compare = upipe_ts_pmtd_compare_header(upipe, header, header_desc,
                                                header_desclength);

    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, uref, header, header_desc,
                               header_desclength)

    if (!compare) {
        struct uref *flow_def = uref_dup(upipe_ts_pmtd->flow_def_input);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
        UBASE_FATAL(upipe, uref_ts_flow_set_pcr_pid(flow_def, pcrpid))
        UBASE_FATAL(upipe, uref_ts_flow_add_descriptor(flow_def, header_desc,
                                                 header_desclength))
        upipe_ts_pmtd_store_flow_def(upipe, flow_def);
        /* Force sending flow def */
        upipe_ts_pmtd_output(upipe, NULL, upump_p);
    }

    upipe_ts_pmtd_clean_flows(upipe);

    UPIPE_TS_PMTD_PEEK(upipe, uref, offset, header_desclength, es, desc,
                       desclength)

    uint16_t pid = pmtn_get_pid(es);
    uint16_t streamtype = pmtn_get_streamtype(es);

    struct uref *flow_def = uref_dup(upipe_ts_pmtd->flow_def_input);
    if (likely(flow_def != NULL)) {
        uref_flow_delete_def(flow_def);
        upipe_ts_pmtd_parse_streamtype(upipe, flow_def, streamtype);
        upipe_ts_pmtd_parse_descs(upipe, flow_def, desc, desclength);
        const char *def;
        if (ubase_check(uref_flow_get_def(flow_def, &def))) {
            UBASE_FATAL(upipe, uref_flow_set_id(flow_def, pid))
            UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def, pid))

            ulist_add(&upipe_ts_pmtd->flows, uref_to_uchain(flow_def));
        } else {
            upipe_warn_va(upipe, "unhandled stream type %u for PID %u",
                          streamtype, pid);
            uref_free(flow_def);
        }
    } else
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);

    UPIPE_TS_PMTD_PEEK_UNMAP(upipe, uref, offset, es, desc, desclength)

    UPIPE_TS_PMTD_PEEK_END(upipe, uref, offset)

    /* Switch tables. */
    if (upipe_ts_pmtd->pmt != NULL)
        uref_free(upipe_ts_pmtd->pmt);
    upipe_ts_pmtd->pmt = uref;

    upipe_split_throw_update(upipe);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_pmtd_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->flow_def_input != NULL)
        uref_free(upipe_ts_pmtd->flow_def_input);
    upipe_ts_pmtd->flow_def_input = flow_def_dup;
    return UBASE_ERR_NONE;
}

/** @internal @This iterates over flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return an error code
 */
static int upipe_ts_pmtd_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    assert(p != NULL);
    struct uchain *uchain;
    if (*p != NULL)
        uchain = uref_to_uchain(*p);
    else
        uchain = &upipe_ts_pmtd->flows;
    if (ulist_is_last(&upipe_ts_pmtd->flows, uchain)) {
        *p = NULL;
        return UBASE_ERR_NONE;
    }
    *p = uref_from_uchain(uchain->next);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_pmtd_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_pmtd_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_pmtd_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_pmtd_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_pmtd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_pmtd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_pmtd_set_output(upipe, output);
        }
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_pmtd_iterate(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pmtd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->pmt != NULL)
        uref_free(upipe_ts_pmtd->pmt);
    if (upipe_ts_pmtd->flow_def_input != NULL)
        uref_free(upipe_ts_pmtd->flow_def_input);
    upipe_ts_pmtd_clean_flows(upipe);
    upipe_ts_pmtd_clean_output(upipe);
    upipe_ts_pmtd_clean_urefcount(upipe);
    upipe_ts_pmtd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pmtd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PMTD_SIGNATURE,

    .upipe_alloc = upipe_ts_pmtd_alloc,
    .upipe_input = upipe_ts_pmtd_input,
    .upipe_control = upipe_ts_pmtd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_pmtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pmtd_mgr_alloc(void)
{
    return &upipe_ts_pmtd_mgr;
}

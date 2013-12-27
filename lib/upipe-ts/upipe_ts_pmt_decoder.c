/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtspmt."
/** max retention time for most streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY UCLOCK_FREQ
/** max retention time for ISO/IEC 14496 streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_14496 (UCLOCK_FREQ * 10)
/** max retention time for still pictures streams (ISO/IEC 13818-1 2.4.2.6) */
#define MAX_DELAY_STILL (UCLOCK_FREQ * 60)

/** @internal @This is the private context of a ts_pmtd pipe. */
struct upipe_ts_pmtd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

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
UPIPE_HELPER_OUTPUT(upipe_ts_pmtd, output, flow_def, flow_def_sent)

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
    bool ret = uref_block_peek_unmap(pmt, 0, header_buffer, header);        \
    if (header_desclength)                                                  \
        ret = uref_block_peek_unmap(pmt, PMT_HEADER_SIZE,                   \
                                    header_desc_buffer, header_desc) && ret;\
    assert(ret);

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
    size_t size;                                                            \
    ret = uref_block_size(pmt, &size);                                      \
    assert(ret);                                                            \
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
        ret = uref_block_peek_unmap(pmt, offset, es_buffer, es);            \
        if (desc != NULL)                                                   \
            ret = uref_block_peek_unmap(pmt, offset + PMT_ES_SIZE,          \
                                        desc_buffer, desc) && ret;          \
        assert(ret);                                                        \
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
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);                         \
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

/** @internal @This is a helper function to determine the maximum retention
 * delay of an h264 elementary stream.
 *
 * @param pmtd_desc_offset offset of the ES descriptors in the uref
 * @param pmtd_desc_size size of the ES descriptors in the uref
 * @return max delay in 27 MHz time scale
 */
static uint64_t upipe_ts_pmtd_h264_max_delay(const uint8_t *descl,
                                             uint16_t desclength)
{
    bool still = true;
    const uint8_t *desc;
    int j = 0;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL)
        if (desc_get_tag(desc) == 0x28 && desc28_validate(desc))
            break;

    if (desc != NULL)
        still = desc28_get_avc_still_present(desc);

    return still ? MAX_DELAY_STILL : MAX_DELAY_14496;
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pmtd_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    assert(upipe_ts_pmtd->flow_def_input != NULL);
    if (upipe_ts_pmtd->pmt != NULL &&
        uref_block_equal(upipe_ts_pmtd->pmt, uref)) {
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
        if (unlikely(!uref_flow_set_def(flow_def, "void.") ||
                     !uref_ts_flow_set_pcr_pid(flow_def, pcrpid) ||
                     !uref_ts_flow_set_descriptors(flow_def, header_desc,
                                                   header_desclength)))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        upipe_ts_pmtd_store_flow_def(upipe, flow_def);
        /* Force sending flow def */
        upipe_throw_new_flow_def(upipe, flow_def);
    }

    upipe_ts_pmtd_clean_flows(upipe);

    UPIPE_TS_PMTD_PEEK(upipe, uref, offset, header_desclength, es, desc,
                       desclength)

    uint16_t pid = pmtn_get_pid(es);
    uint16_t streamtype = pmtn_get_streamtype(es);

    struct uref *flow_def = uref_dup(upipe_ts_pmtd->flow_def_input);
    if (likely(flow_def != NULL)) {
        switch (streamtype) {
            case PMT_STREAMTYPE_VIDEO_MPEG1:
                if (unlikely(!uref_flow_set_def(flow_def,
                                "block.mpeg1video.pic.") ||
                             !uref_flow_set_id(flow_def, pid) ||
                             !uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.mpeg1video.pic.") ||
                             !uref_ts_flow_set_pid(flow_def, pid) ||
                             !uref_ts_flow_set_descriptors(flow_def, desc,
                                                           desclength) ||
                             !uref_ts_flow_set_max_delay(flow_def,
                                MAX_DELAY_STILL)))
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                ulist_add(&upipe_ts_pmtd->flows, uref_to_uchain(flow_def));
                break;

            case PMT_STREAMTYPE_VIDEO_MPEG2:
                if (unlikely(!uref_flow_set_def(flow_def,
                                "block.mpeg2video.pic.") ||
                             !uref_flow_set_id(flow_def, pid) ||
                             !uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.mpeg2video.pic.") ||
                             !uref_ts_flow_set_pid(flow_def, pid) ||
                             !uref_ts_flow_set_descriptors(flow_def, desc,
                                                           desclength) ||
                             !uref_ts_flow_set_max_delay(flow_def,
                                 MAX_DELAY_STILL)))
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                ulist_add(&upipe_ts_pmtd->flows, uref_to_uchain(flow_def));
                break;

            case PMT_STREAMTYPE_AUDIO_MPEG1:
            case PMT_STREAMTYPE_AUDIO_MPEG2:
                if (unlikely(!uref_flow_set_def(flow_def, "block.mp2.sound.") ||
                             !uref_flow_set_id(flow_def, pid) ||
                             !uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.mp2.sound.") ||
                             !uref_ts_flow_set_pid(flow_def, pid) ||
                             !uref_ts_flow_set_descriptors(flow_def, desc,
                                                           desclength) ||
                             !uref_ts_flow_set_max_delay(flow_def, MAX_DELAY)))
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                ulist_add(&upipe_ts_pmtd->flows, uref_to_uchain(flow_def));
                break;

            case PMT_STREAMTYPE_AUDIO_ADTS:
                if (unlikely(!uref_flow_set_def(flow_def, "block.aac.sound.") ||
                             !uref_flow_set_id(flow_def, pid) ||
                             !uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.aac.sound.") ||
                             !uref_ts_flow_set_pid(flow_def, pid) ||
                             !uref_ts_flow_set_descriptors(flow_def, desc,
                                                           desclength) ||
                             !uref_ts_flow_set_max_delay(flow_def, MAX_DELAY)))
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                ulist_add(&upipe_ts_pmtd->flows, uref_to_uchain(flow_def));
                break;

            case PMT_STREAMTYPE_VIDEO_AVC:
                if (unlikely(!uref_flow_set_def(flow_def, "block.h264.pic.") ||
                             !uref_flow_set_id(flow_def, pid) ||
                             !uref_flow_set_raw_def(flow_def,
                                "block.mpegts.mpegtspes.h264.pic.") ||
                             !uref_ts_flow_set_pid(flow_def, pid) ||
                             !uref_ts_flow_set_max_delay(flow_def,
                                upipe_ts_pmtd_h264_max_delay(desc,
                                    desclength))))
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                ulist_add(&upipe_ts_pmtd->flows, uref_to_uchain(flow_def));
                break;

            default:
                upipe_warn_va(upipe, "unhandled stream type %u for PID %u",
                              streamtype, pid);
                uref_free(flow_def);
                break;
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
 * @return false if the flow definition is not handled
 */
static bool upipe_ts_pmtd_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return false;
    if (!uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
        return false;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->flow_def_input != NULL)
        uref_free(upipe_ts_pmtd->flow_def_input);
    upipe_ts_pmtd->flow_def_input = flow_def_dup;
    return true;
}

/** @internal @This iterates over flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return false when no more flow definition is available
 */
static bool upipe_ts_pmtd_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    assert(p != NULL);
    struct uchain *uchain;
    if (*p != NULL)
        uchain = uref_to_uchain(*p);
    else
        uchain = &upipe_ts_pmtd->flows;
    if (ulist_is_last(&upipe_ts_pmtd->flows, uchain))
        return false;
    *p = uref_from_uchain(uchain->next);
    return true;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_pmtd_control(struct upipe *upipe,
                                  enum upipe_command command,
                                  va_list args)
{
    switch (command) {
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
            return false;
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
    .upipe_control = upipe_ts_pmtd_control
};

/** @This returns the management structure for all ts_pmtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pmtd_mgr_alloc(void)
{
    return &upipe_ts_pmtd_mgr;
}

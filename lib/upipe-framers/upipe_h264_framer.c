/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of an ISO 14496-10-B stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-framers/upipe_h264_framer.h>
#if 0
#include <upipe-framers/uref_h264.h>
#include <upipe-framers/uref_h264_flow.h>
#endif

#include "upipe_framers_common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/h264.h>

/** we only accept the ISO 14496-10 annex B elementary stream */
#define EXPECTED_FLOW_DEF "block.h264."

/** @internal @This translates the MPEG frame_rate_code to double */
static const struct urational frame_rate_from_code[] = {
    { .num = 0, .den = 0 }, /* invalid */
    { .num = 24000, .den = 1001 },
    { .num = 24, .den = 1 },
    { .num = 25, .den = 1 },
    { .num = 30000, .den = 1001 },
    { .num = 30, .den = 1 },
    { .num = 50, .den = 1 },
    { .num = 60000, .den = 1001 },
    { .num = 60, .den = 1 },
    /* Xing */
    { .num = 15000, .den = 1001 },
    /* libmpeg3 */
    { .num = 5000, .den = 1001 },
    { .num = 10000, .den = 1001 },
    { .num = 12000, .den = 1001 },
    { .num = 15000, .den = 1001 },
    /* invalid */
    { .num = 0, .den = 0 },
    { .num = 0, .den = 0 }
};

/** @internal @This is the private context of an h264f pipe. */
struct upipe_h264f {
    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** last random access point */
    uint64_t systime_rap;

    /* picture parsing stuff */
    /** last output picture number */
    uint64_t last_picture_number;
    /** true we have had a discontinuity recently */
    bool got_discontinuity;
    /** pointers to sequence parameter sets */
    struct ubuf *sps[H264SPS_ID_MAX];
    /** pointers to sequence parameter set extensions */
    struct ubuf *sps_ext[H264SPS_ID_MAX];
    /** pointers to subset sequence parameter sets */
    struct ubuf *sps_subset[H264SPS_ID_MAX];
    /** active sequence parameter set, or -1 */
    int active_sps;
    /** pointers to picture parameter sets */
    struct ubuf *pps[H264PPS_ID_MAX];
    /** active picture parameter set, or -1 */
    int active_pps;

    /* octet stream stuff */
    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct ulist urefs;

    /* octet stream parser stuff */
    /** context of the scan function */
    uint32_t scan_context;
    /** current size of next access unit (in next_uref) */
    size_t au_size;
    /** last NAL offset in the access unit, or -1 */
    ssize_t au_last_nal_offset;
    /** last NAL start code in the access unit */
    uint8_t au_last_nal;
    /** size of the last NAL start code in the access unit */
    size_t au_last_nal_start_size;
    /** offset of the first VCL NAL in next_uref, or -1 */
    ssize_t au_vcl_offset;
    /** NAL start code of the first slice, or 0xff */
    uint8_t au_slice_nal;
    /** original PTS of the next picture, or UINT64_MAX */
    uint64_t au_pts_orig;
    /** PTS of the next picture, or UINT64_MAX */
    uint64_t au_pts;
    /** system PTS of the next picture, or UINT64_MAX */
    uint64_t au_pts_sys;
    /** original DTS of the next picture, or UINT64_MAX */
    uint64_t au_dts_orig;
    /** DTS of the next picture, or UINT64_MAX */
    uint64_t au_dts;
    /** system DTS of the next picture, or UINT64_MAX */
    uint64_t au_dts_sys;
    /** true if we have thrown the sync_acquired event (that means we found a
     * NAL start) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_h264f_promote_uref(struct upipe *upipe);

UPIPE_HELPER_UPIPE(upipe_h264f, upipe)
UPIPE_HELPER_SYNC(upipe_h264f, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_h264f, next_uref, next_uref_size, urefs,
                         upipe_h264f_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_h264f, output, flow_def, flow_def_sent)

/** @internal @This flushes all PTS timestamps.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_flush_pts(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_h264f->au_pts_orig = UINT64_MAX;
    upipe_h264f->au_pts = UINT64_MAX;
    upipe_h264f->au_pts_sys = UINT64_MAX;
}

/** @internal @This flushes all DTS timestamps.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_flush_dts(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_h264f->au_dts_orig = UINT64_MAX;
    upipe_h264f->au_dts = UINT64_MAX;
    upipe_h264f->au_dts_sys = UINT64_MAX;
}

/** @internal @This increments all DTS timestamps by the duration of the frame.
 *
 * @param upipe description structure of the pipe
 * @param duration duration of the frame
 */
static void upipe_h264f_increment_dts(struct upipe *upipe, uint64_t duration)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->au_dts_orig != UINT64_MAX)
        upipe_h264f->au_dts_orig += duration;
    if (upipe_h264f->au_dts != UINT64_MAX)
        upipe_h264f->au_dts += duration;
    if (upipe_h264f->au_dts_sys != UINT64_MAX)
        upipe_h264f->au_dts_sys += duration;
}

/** @internal @This is called back by @ref upipe_h264f_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_promote_uref(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint64_t ts;
#define SET_TIMESTAMP(name)                                                 \
    if (uref_clock_get_##name(upipe_h264f->next_uref, &ts))                 \
        upipe_h264f->au_##name = ts;
    SET_TIMESTAMP(pts_orig)
    SET_TIMESTAMP(pts)
    SET_TIMESTAMP(pts_sys)
    SET_TIMESTAMP(dts_orig)
    SET_TIMESTAMP(dts)
    SET_TIMESTAMP(dts_sys)
#undef SET_TIMESTAMP
}

/** @internal @This allocates an h264f pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_h264f_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe)
{
    struct upipe_h264f *upipe_h264f = malloc(sizeof(struct upipe_h264f));
    if (unlikely(upipe_h264f == NULL))
        return NULL;
    struct upipe *upipe = upipe_h264f_to_upipe(upipe_h264f);
    upipe_init(upipe, mgr, uprobe);
    upipe_h264f_init_sync(upipe);
    upipe_h264f_init_uref_stream(upipe);
    upipe_h264f_init_output(upipe);
    upipe_h264f->flow_def_input = NULL;
    upipe_h264f->systime_rap = UINT64_MAX;
    upipe_h264f->last_picture_number = 0;
    upipe_h264f->got_discontinuity = false;
    upipe_h264f->scan_context = UINT32_MAX;
    upipe_h264f->au_size = 0;
    upipe_h264f->au_last_nal_offset = -1;
    upipe_h264f->au_last_nal = UINT8_MAX;
    upipe_h264f->au_last_nal_start_size = 0;
    upipe_h264f->au_vcl_offset = -1;
    upipe_h264f->au_slice_nal = UINT8_MAX;
    upipe_h264f_flush_pts(upipe);
    upipe_h264f_flush_dts(upipe);

    int i;
    for (i = 0; i < H264SPS_ID_MAX; i++) {
        upipe_h264f->sps[i] = NULL;
        upipe_h264f->sps_ext[i] = NULL;
        upipe_h264f->sps_subset[i] = NULL;
    }
    upipe_h264f->active_sps = -1;

    for (i = 0; i < H264PPS_ID_MAX; i++)
        upipe_h264f->pps[i] = NULL;
    upipe_h264f->active_pps = -1;

    upipe_h264f->acquired = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This finds an MPEG-2 start code and returns its value.
 *
 * @param upipe description structure of the pipe
 * @param start_p filled in with the value of the start code
 * @param prev_p filled in with the value of the previous octet, if applicable
 * @return true if a start code was found
 */
static bool upipe_h264f_find(struct upipe *upipe,
                             uint8_t *start_p, uint8_t *prev_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    const uint8_t *buffer;
    int size = -1;
    while (uref_block_read(upipe_h264f->next_uref, upipe_h264f->au_size,
                           &size, &buffer)) {
        const uint8_t *p = upipe_framers_mpeg_scan(buffer, buffer + size,
                                                   &upipe_h264f->scan_context);
        if (p > buffer + 4)
            *prev_p = p[-4];
        uref_block_unmap(upipe_h264f->next_uref, upipe_h264f->au_size);

        if ((upipe_h264f->scan_context & 0xffffff00) == 0x100) {
            *start_p = upipe_h264f->scan_context & 0xff;
            upipe_h264f->au_size += p - buffer;
            if (p <= buffer + 4 &&
                !uref_block_extract(upipe_h264f->next_uref,
                                    upipe_h264f->au_size - 4, 1, prev_p))
                *prev_p = 0xff;
            return true;
        }
        upipe_h264f->au_size += size;
        size = -1;
    }
    return false;
}

/** @internal @This handles a sequence parameter set.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sps(struct upipe *upipe)
{
}

/** @internal @This handles a sequence parameter set extension.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sps_ext(struct upipe *upipe)
{
}

/** @internal @This handles a subset sequence parameter set.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sps_subset(struct upipe *upipe)
{
}

/** @internal @This handles a picture parameter set.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_pps(struct upipe *upipe)
{
}

/** @internal @This handles a supplemental enhancement information.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sei(struct upipe *upipe)
{
}

/** @internal @This parses a slice header.
 *
 * @param upipe description structure of the pipe
 * @return false if the slice doesn't belong to the existing access unit
 */
static bool upipe_h264f_parse_slice(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->flow_def == NULL) {
        struct uref *flow_def = uref_dup(upipe_h264f->flow_def_input);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_aerror(upipe);
            return true;
        }
        bool ret = true;
        if (unlikely(!ret)) {
            upipe_throw_aerror(upipe);
            return true;
        }
        upipe_h264f_store_flow_def(upipe, flow_def);
    }
    return true;
}

/** @internal @This handles and outputs an access unit.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_output_au(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (!upipe_h264f->au_size)
        return;

    struct uref *uref = upipe_h264f_extract_uref_stream(upipe,
                                                         upipe_h264f->au_size);
    upipe_h264f->au_size = 0;
    upipe_h264f->au_vcl_offset = -1;
    upipe_h264f->au_slice_nal = UINT8_MAX;
    if (unlikely(uref == NULL)) {
        upipe_throw_aerror(upipe);
        return;
    }

    bool ret = true;
#define SET_TIMESTAMP(name)                                                 \
    if (upipe_h264f->au_##name != UINT64_MAX)                               \
        ret = ret && uref_clock_set_##name(uref, upipe_h264f->au_##name);   \
    else                                                                    \
        uref_clock_delete_##name(uref);
    SET_TIMESTAMP(pts_orig)
    SET_TIMESTAMP(pts)
    SET_TIMESTAMP(pts_sys)
    SET_TIMESTAMP(dts_orig)
    SET_TIMESTAMP(dts)
    SET_TIMESTAMP(dts_sys)
#undef SET_TIMESTAMP
    upipe_h264f_flush_pts(upipe);
    upipe_h264f_flush_dts(upipe);

    if (upipe_h264f->systime_rap != UINT64_MAX)
        ret = ret && uref_clock_set_systime_rap(uref, upipe_h264f->systime_rap);
    if (upipe_h264f->au_vcl_offset > 0)
        ret = ret && uref_block_set_header_size(uref,
                                                upipe_h264f->au_vcl_offset);

    if (unlikely(!ret)) {
        uref_free(uref);
        upipe_throw_aerror(upipe);
        return;
    }

    upipe_h264f_output(upipe, uref, upump);
}

/** @internal @This is called when a new NAL starts, to check the previous NAL.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_nal_end(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (unlikely(!upipe_h264f->acquired)) {
        /* we need to discard previous data */
        upipe_h264f_consume_uref_stream(upipe, upipe_h264f->au_size);
        upipe_h264f->au_size = 0;
        upipe_h264f_sync_acquired(upipe);
        return;
    }
    if (upipe_h264f->au_last_nal_offset == -1)
        return;

    uint8_t last_nal_type = h264nalst_get_type(upipe_h264f->au_last_nal);
    if (last_nal_type == H264SLI_NONIDR_START_CODE ||
        last_nal_type == H264SLI_PARTA_START_CODE ||
        last_nal_type == H264SLI_PARTB_START_CODE ||
        last_nal_type == H264SLI_PARTC_START_CODE ||
        last_nal_type == H264SLI_IDR_START_CODE) {
        if (unlikely(upipe_h264f->got_discontinuity))
            uref_flow_set_error(upipe_h264f->next_uref);
        else if (!upipe_h264f_parse_slice(upipe)) {
            /* the last slice doesn't belong to the access unit */
            size_t slice_size = upipe_h264f->au_size -
                                upipe_h264f->au_last_nal_offset;
            upipe_h264f->au_size = upipe_h264f->au_last_nal_offset;
            upipe_h264f_output_au(upipe, upump);
            upipe_h264f->au_size = slice_size;
        }
        if (last_nal_type == H264SLI_IDR_START_CODE)
            uref_clock_get_systime_rap(upipe_h264f->next_uref,
                                       &upipe_h264f->systime_rap);
        return;
    }

    if (upipe_h264f->got_discontinuity) {
        uref_flow_set_error(upipe_h264f->next_uref);
        /* discard the entire NAL */
        uref_block_delete(upipe_h264f->next_uref,
                      upipe_h264f->au_last_nal_offset,
                      upipe_h264f->au_size - upipe_h264f->au_last_nal_offset);
        return;
    }

    switch (last_nal_type) {
        case H264SEI_START_CODE:
            upipe_h264f_handle_sei(upipe);
            break;
        case H264SPS_START_CODE:
            upipe_h264f_handle_sps(upipe);
            break;
        case H264SPSX_START_CODE:
            upipe_h264f_handle_sps_ext(upipe);
            break;
        case H264SSPS_START_CODE:
            upipe_h264f_handle_sps_subset(upipe);
            break;
        case H264PPS_START_CODE:
            upipe_h264f_handle_pps(upipe);
            break;
        default:
            break;
    }
}

/** @internal @This is called when a new NAL starts, to check it.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 * @param true if the NAL was completely handled
 */
static bool upipe_h264f_nal_begin(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    /* detection of a new access unit - ISO/IEC 14496-10 7.4.1.2.4 */
    uint8_t nal_type = h264nalst_get_type(upipe_h264f->au_last_nal);
    switch (nal_type) {
        case H264SLI_NONIDR_START_CODE:
        case H264SLI_PARTA_START_CODE:
        case H264SLI_PARTB_START_CODE:
        case H264SLI_PARTC_START_CODE:
        case H264SLI_IDR_START_CODE: {
            uint8_t slice_nal = upipe_h264f->au_slice_nal;
            upipe_h264f->au_slice_nal = upipe_h264f->au_last_nal;

            if (upipe_h264f->au_vcl_offset == -1) {
                upipe_h264f->au_vcl_offset =
                    upipe_h264f->au_size - upipe_h264f->au_last_nal_start_size;
                return false;
            }
            if (slice_nal == UINT8_MAX)
                return false;
            if ((h264nalst_get_type(slice_nal) == H264SLI_IDR_START_CODE) ==
                    (nal_type == H264SLI_IDR_START_CODE) &&
                (h264nalst_get_ref(upipe_h264f->au_last_nal) == 0) ==
                    (h264nalst_get_ref(slice_nal) == 0))
                return false;
            /* new access unit */
            break;
        }

        case H264SEI_START_CODE:
            if (upipe_h264f->au_vcl_offset == -1) {
                upipe_h264f->au_vcl_offset =
                    upipe_h264f->au_size - upipe_h264f->au_last_nal_start_size;
                return false;
            }
            if (upipe_h264f->au_slice_nal == UINT8_MAX)
                return false;
            break;

        case H264ENDSEQ_START_CODE:
        case H264ENDSTR_START_CODE:
            /* immediately output everything and jump out */
            upipe_h264f_output_au(upipe, upump);
            return true;

        case H264AUD_START_CODE:
        case H264SPS_START_CODE:
        case H264SPSX_START_CODE:
        case H264SSPS_START_CODE:
        case H264PPS_START_CODE:
            if (upipe_h264f->au_slice_nal == UINT8_MAX)
                return false;
            break;

        default:
            if (nal_type < 14 || nal_type > 18 ||
                upipe_h264f->au_slice_nal == UINT8_MAX)
                return false;
            break;
    }

    upipe_h264f->au_size -= upipe_h264f->au_last_nal_start_size;
    upipe_h264f_output_au(upipe, upump);
    upipe_h264f->au_size = upipe_h264f->au_last_nal_start_size;
    return false;
}

/** @internal @This tries to output access units from the queue of input
 * buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_work(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    while (upipe_h264f->next_uref != NULL) {
        uint8_t start, prev;
        if (!upipe_h264f_find(upipe, &start, &prev))
            return;
        size_t start_size = !prev ? 5 : 4;
        //upipe_err_va(upipe, "pouet %x", start);

        upipe_h264f->au_size -= start_size;
        upipe_h264f_nal_end(upipe, upump);
        upipe_h264f->au_size += start_size;
        upipe_h264f->got_discontinuity = false;
        upipe_h264f->au_last_nal = start;
        upipe_h264f->au_last_nal_start_size = start_size;
        if (upipe_h264f_nal_begin(upipe, upump))
            upipe_h264f->au_last_nal_offset = -1;
        else
            upipe_h264f->au_last_nal_offset = upipe_h264f->au_size - start_size;
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            uref_free(uref);
            if (upipe_h264f->flow_def_input != NULL) {
                uref_free(upipe_h264f->flow_def_input);
                upipe_h264f->flow_def_input = NULL;
            }
            upipe_h264f_store_flow_def(upipe, NULL);
            upipe_throw_flow_def_error(upipe, uref);
            return;
        }

        upipe_dbg_va(upipe, "flow definition: %s", def);
        upipe_h264f->flow_def_input = uref;
        return;
    }

    if (unlikely(uref_flow_get_end(uref))) {
        uref_free(uref);
        upipe_throw_need_input(upipe);
        return;
    }

    if (unlikely(upipe_h264f->flow_def_input == NULL)) {
        upipe_throw_flow_def_error(upipe, uref);
        uref_free(uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    if (unlikely(uref_flow_get_discontinuity(uref)))
        upipe_h264f->got_discontinuity = true;

    upipe_h264f_append_uref_stream(upipe, uref);
    upipe_h264f_work(upipe, upump);
}

/** @internal @This processes control commands on a h264f pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_h264f_control(struct upipe *upipe,
                                enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_h264f_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_h264f_set_output(upipe, output);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_free(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_h264f_clean_uref_stream(upipe);
    upipe_h264f_clean_output(upipe);
    upipe_h264f_clean_sync(upipe);

    if (upipe_h264f->flow_def_input != NULL)
        uref_free(upipe_h264f->flow_def_input);

    int i;
    for (i = 0; i < H264SPS_ID_MAX; i++) {
        if (upipe_h264f->sps[i] != NULL)
            ubuf_free(upipe_h264f->sps[i]);
        if (upipe_h264f->sps_ext[i] != NULL)
            ubuf_free(upipe_h264f->sps_ext[i]);
        if (upipe_h264f->sps_subset[i] != NULL)
            ubuf_free(upipe_h264f->sps_subset[i]);
    }

    for (i = 0; i < H264PPS_ID_MAX; i++)
        if (upipe_h264f->pps[i] != NULL)
            ubuf_free(upipe_h264f->pps[i]);

    upipe_clean(upipe);
    free(upipe_h264f);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_h264f_mgr = {
    .signature = UPIPE_H264F_SIGNATURE,

    .upipe_alloc = upipe_h264f_alloc,
    .upipe_input = upipe_h264f_input,
    .upipe_control = upipe_h264f_control,
    .upipe_free = upipe_h264f_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all h264f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_h264f_mgr_alloc(void)
{
    return &upipe_h264f_mgr;
}

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
 * @short Upipe module building frames from chunks of an ISO 13818-2 stream
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/uref_mpgv.h>
#include <upipe-framers/uref_mpgv_flow.h>

#include "upipe_framers_common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/mp2v.h>

/** @internal @This translates the MPEG frame_rate_code to urational. */
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

/** @internal @This is the private context of an mpgvf pipe. */
struct upipe_mpgvf {
    /** refcount management structure */
    struct urefcount urefcount;

    /* output stuff */
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** attributes in the sequence header */
    struct uref *flow_def_attr;
    /** requested flow definition */
    struct uref *flow_def_requested;
    /** true if we have to insert sequence headers before I frames,
     * if it is not already present */
    bool sequence_requested;

    /** flow format request */
    struct urequest request;
    /** temporary uref storage (used during urequest) */
    struct uchain request_urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during urequest) */
    struct uchain blockers;

    /** rap of the last dts */
    uint64_t dts_rap;
    /** rap of the last sequence header */
    uint64_t seq_rap;
    /** rap of the last I */
    uint64_t iframe_rap;
    /** rap of the last reference frame */
    uint64_t ref_rap;
    /** latency in the input flow */
    uint64_t input_latency;

    /* picture parsing stuff */
    /** last output picture number */
    uint64_t last_picture_number;
    /** last temporal reference read from the stream, or -1 */
    int last_temporal_reference;
    /** true we have had a discontinuity recently */
    bool got_discontinuity;
    /** pointer to a sequence header */
    struct ubuf *sequence_header;
    /** pointer to a sequence header extension */
    struct ubuf *sequence_ext;
    /** pointer to a sequence display extension */
    struct ubuf *sequence_display;
    /** true if the flag progressive sequence is true */
    bool progressive_sequence;
    /** frames per second */
    struct urational fps;
    /** closed GOP */
    bool closed_gop;
    /** sample aspect ratio */
    struct urational sar;

    /* octet stream stuff */
    /** next uref to be processed */
    struct uref *next_uref;
    /** original size of the next uref */
    size_t next_uref_size;
    /** urefs received after next uref */
    struct uchain urefs;

    /* octet stream parser stuff */
    /** context of the scan function */
    uint32_t scan_context;
    /** current size of next frame (in next_uref) */
    size_t next_frame_size;
    /** true if the next uref begins with a sequence header */
    bool next_frame_sequence;
    /** offset of the sequence extension in next_uref, or -1 */
    ssize_t next_frame_sequence_ext_offset;
    /** offset of the sequence display in next_uref, or -1 */
    ssize_t next_frame_sequence_display_offset;
    /** offset of the GOP header in next_uref, or -1 */
    ssize_t next_frame_gop_offset;
    /** offset of the picture header in next_uref, or -1 */
    ssize_t next_frame_offset;
    /** offset of the picture extension in next_uref, or -1 */
    ssize_t next_frame_ext_offset;
    /** true if we have found at least one slice header */
    bool next_frame_slice;
    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** drift rate of the next picture */
    struct urational drift_rate;
    /** true if we have thrown the sync_acquired event (that means we found a
     * sequence header) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_mpgvf_promote_uref(struct upipe *upipe);
/** @hidden */
static bool upipe_mpgvf_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p);
/** @hidden */
static int upipe_mpgvf_check_flow_format(struct upipe *upipe,
                                         struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_mpgvf, upipe, UPIPE_MPGVF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_mpgvf, urefcount, upipe_mpgvf_free)
UPIPE_HELPER_VOID(upipe_mpgvf)
UPIPE_HELPER_SYNC(upipe_mpgvf, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_mpgvf, next_uref, next_uref_size, urefs,
                         upipe_mpgvf_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_mpgvf, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_mpgvf, request_urefs, nb_urefs, max_urefs, blockers, upipe_mpgvf_handle)
UPIPE_HELPER_FLOW_FORMAT(upipe_mpgvf, request, upipe_mpgvf_check_flow_format,
                         upipe_mpgvf_register_output_request,
                         upipe_mpgvf_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_mpgvf, flow_def_input, flow_def_attr)

/** @internal @This flushes all dates.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgvf_flush_dates(struct upipe *upipe)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    uref_clock_set_date_sys(&upipe_mpgvf->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_mpgvf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_mpgvf->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_delete_dts_pts_delay(&upipe_mpgvf->au_uref_s);
}

/** @internal @This allocates an mpgvf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_mpgvf_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_mpgvf_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    upipe_mpgvf_init_urefcount(upipe);
    upipe_mpgvf_init_sync(upipe);
    upipe_mpgvf_init_uref_stream(upipe);
    upipe_mpgvf_init_output(upipe);
    upipe_mpgvf_init_input(upipe);
    upipe_mpgvf_init_flow_format(upipe);
    upipe_mpgvf_init_flow_def(upipe);
    upipe_mpgvf->flow_def_requested = NULL;
    upipe_mpgvf->sequence_requested = false;
    upipe_mpgvf->dts_rap = UINT64_MAX;
    upipe_mpgvf->seq_rap = UINT64_MAX;
    upipe_mpgvf->iframe_rap = UINT64_MAX;
    upipe_mpgvf->ref_rap = UINT64_MAX;
    upipe_mpgvf->input_latency = 0;
    upipe_mpgvf->last_picture_number = 0;
    upipe_mpgvf->last_temporal_reference = -1;
    upipe_mpgvf->got_discontinuity = false;
    upipe_mpgvf->fps.num = 0;
    upipe_mpgvf->scan_context = UINT32_MAX;
    upipe_mpgvf->next_frame_size = 0;
    upipe_mpgvf->next_frame_sequence = false;
    upipe_mpgvf->next_frame_sequence_ext_offset = -1;
    upipe_mpgvf->next_frame_sequence_display_offset = -1;
    upipe_mpgvf->next_frame_gop_offset = -1;
    upipe_mpgvf->next_frame_offset = -1;
    upipe_mpgvf->next_frame_ext_offset = -1;
    upipe_mpgvf->next_frame_slice = false;
    uref_init(&upipe_mpgvf->au_uref_s);
    upipe_mpgvf_flush_dates(upipe);
    upipe_mpgvf->drift_rate.num = upipe_mpgvf->drift_rate.den = 0;
    upipe_mpgvf->sequence_header = upipe_mpgvf->sequence_ext =
        upipe_mpgvf->sequence_display = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This finds an MPEG-2 start code and returns its value.
 *
 * @param upipe description structure of the pipe
 * @param start_p filled in with the value of the start code
 * @param next_p filled in with the value of the extension code, if applicable
 * @return true if a start code was found
 */
static bool upipe_mpgvf_find(struct upipe *upipe,
                             uint8_t *start_p, uint8_t *next_p)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    const uint8_t *buffer;
    int size = -1;
    while (ubase_check(uref_block_read(upipe_mpgvf->next_uref,
                    upipe_mpgvf->next_frame_size, &size, &buffer))) {
        const uint8_t *p = upipe_framers_mpeg_scan(buffer, buffer + size,
                                                   &upipe_mpgvf->scan_context);
        if (p < buffer + size)
            *next_p = *p;
        uref_block_unmap(upipe_mpgvf->next_uref, upipe_mpgvf->next_frame_size);

        if ((upipe_mpgvf->scan_context & 0xffffff00) == 0x100) {
            *start_p = upipe_mpgvf->scan_context & 0xff;
            upipe_mpgvf->next_frame_size += p - buffer;
            if (*start_p == MP2VX_START_CODE && p >= buffer + size &&
                !ubase_check(uref_block_extract(upipe_mpgvf->next_uref,
                                    upipe_mpgvf->next_frame_size, 1, next_p))) {
                upipe_mpgvf->scan_context = UINT32_MAX;
                upipe_mpgvf->next_frame_size -= 4;
                return false;
            }
            return true;
        }
        upipe_mpgvf->next_frame_size += size;
        size = -1;
    }
    return false;
}

/** @internal @This parses a new sequence header, and outputs a flow
 * definition
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_mpgvf_parse_sequence(struct upipe *upipe)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    uint8_t sequence_buffer[MP2VSEQ_HEADER_SIZE];
    const uint8_t *sequence;
    if (unlikely((sequence = ubuf_block_peek(upipe_mpgvf->sequence_header,
                                             0, MP2VSEQ_HEADER_SIZE,
                                             sequence_buffer)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    uint16_t horizontal = mp2vseq_get_horizontal(sequence);
    uint16_t vertical = mp2vseq_get_vertical(sequence);
    uint8_t aspect = mp2vseq_get_aspect(sequence);
    uint8_t framerate = mp2vseq_get_framerate(sequence);
    uint32_t bitrate = mp2vseq_get_bitrate(sequence);
    uint32_t vbvbuffer = mp2vseq_get_vbvbuffer(sequence);
    UBASE_FATAL(upipe, ubuf_block_peek_unmap(upipe_mpgvf->sequence_header, 0,
                                             sequence_buffer, sequence))

    struct urational frame_rate = frame_rate_from_code[framerate];
    if (!frame_rate.num) {
        upipe_err_va(upipe, "invalid frame rate %d", framerate);
        return false;
    }

    struct uref *flow_def = upipe_mpgvf_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    uint64_t max_octetrate = 1500000 / 8;
    bool progressive = true;
    uint8_t chroma = MP2VSEQX_CHROMA_420;
    if (upipe_mpgvf->sequence_ext != NULL) {
        uint8_t ext_buffer[MP2VSEQX_HEADER_SIZE];
        const uint8_t *ext;
        if (unlikely((ext = ubuf_block_peek(upipe_mpgvf->sequence_ext,
                                            0, MP2VSEQX_HEADER_SIZE,
                                            ext_buffer)) == NULL)) {
            uref_free(flow_def);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }

        uint8_t profilelevel = mp2vseqx_get_profilelevel(ext);
        progressive = mp2vseqx_get_progressive(ext);
        chroma = mp2vseqx_get_chroma(ext);
        horizontal |= mp2vseqx_get_horizontal(ext) << 12;
        vertical |= mp2vseqx_get_vertical(ext) << 12;
        bitrate |= mp2vseqx_get_bitrate(ext) << 18;
        vbvbuffer |= mp2vseqx_get_vbvbuffer(ext) << 10;
        bool lowdelay = mp2vseqx_get_lowdelay(ext);
        frame_rate.num *= mp2vseqx_get_frameraten(ext) + 1;
        frame_rate.den *= mp2vseqx_get_framerated(ext) + 1;
        urational_simplify(&frame_rate);

        UBASE_FATAL(upipe, ubuf_block_peek_unmap(upipe_mpgvf->sequence_ext, 0,
                                                 ext_buffer, ext));
        UBASE_FATAL(upipe, uref_mpgv_flow_set_profilelevel(flow_def, profilelevel))
        switch (profilelevel & MP2VSEQX_LEVEL_MASK) {
            case MP2VSEQX_LEVEL_LOW:
                max_octetrate = 4000000 / 8;
                break;
            case MP2VSEQX_LEVEL_MAIN:
                max_octetrate = 15000000 / 8;
                break;
            case MP2VSEQX_LEVEL_HIGH1440:
                max_octetrate = 60000000 / 8;
                break;
            case MP2VSEQX_LEVEL_HIGH:
                max_octetrate = 80000000 / 8;
                break;
            case MP2VSEQX_LEVEL_HIGHP:
                max_octetrate = 120000000 / 8;
                break;
            default:
                upipe_err_va(upipe, "invalid level %d",
                             profilelevel & MP2VSEQX_LEVEL_MASK);
                uref_free(flow_def);
                return false;
        }
        if (lowdelay)
            UBASE_FATAL(upipe, uref_flow_set_lowdelay(flow_def))
    } else
        upipe_mpgvf->progressive_sequence = true;

    UBASE_FATAL(upipe, uref_pic_flow_set_fps(flow_def, frame_rate))
    UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                        upipe_mpgvf->input_latency +
                        UCLOCK_FREQ * frame_rate.den / frame_rate.num))
    UBASE_FATAL(upipe, uref_block_flow_set_max_octetrate(flow_def, max_octetrate))
    upipe_mpgvf->progressive_sequence = progressive;
    UBASE_FATAL(upipe, uref_pic_flow_set_macropixel(flow_def, 1))
    UBASE_FATAL(upipe, uref_pic_flow_set_planes(flow_def, 0))
    UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
    switch (chroma) {
        case MP2VSEQX_CHROMA_420:
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"))
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"))
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                    UPIPE_MPGVF_EXPECTED_FLOW_DEF "pic.planar8_8_420."))
            break;
        case MP2VSEQX_CHROMA_422:
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 2, 1, 1, "u8"))
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 2, 1, 1, "v8"))
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                    UPIPE_MPGVF_EXPECTED_FLOW_DEF "pic.planar8_8_422."))
            break;
        case MP2VSEQX_CHROMA_444:
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "u8"))
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "v8"))
            UBASE_FATAL(upipe, uref_flow_set_def(flow_def,
                    UPIPE_MPGVF_EXPECTED_FLOW_DEF "pic.planar8_8_444."))
            break;
        default:
            upipe_err_va(upipe, "invalid chroma format %d", chroma);
            uref_free(flow_def);
            return false;
    }

    UBASE_FATAL(upipe, uref_pic_flow_set_hsize(flow_def, horizontal))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def, vertical))
    switch (aspect) {
        case MP2VSEQ_ASPECT_SQUARE:
            upipe_mpgvf->sar.num = upipe_mpgvf->sar.den = 1;
            break;
        case MP2VSEQ_ASPECT_4_3:
            upipe_mpgvf->sar.num = vertical * 4;
            upipe_mpgvf->sar.den = horizontal * 3;
            urational_simplify(&upipe_mpgvf->sar);
            break;
        case MP2VSEQ_ASPECT_16_9:
            upipe_mpgvf->sar.num = vertical * 16;
            upipe_mpgvf->sar.den = horizontal * 9;
            urational_simplify(&upipe_mpgvf->sar);
            break;
        case MP2VSEQ_ASPECT_2_21:
            upipe_mpgvf->sar.num = vertical * 221;
            upipe_mpgvf->sar.den = horizontal * 100;
            urational_simplify(&upipe_mpgvf->sar);
            break;
        default:
            upipe_err_va(upipe, "invalid aspect ratio %d", aspect);
            uref_free(flow_def);
            return false;
    }
    UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def, upipe_mpgvf->sar))
    upipe_mpgvf->fps = frame_rate;
    if (max_octetrate < (uint64_t)bitrate * 400 / 8)
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def,
                                                         max_octetrate))
    else {
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(
                flow_def, (uint64_t)bitrate * 400 / 8))
    }
    UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def,
                                                vbvbuffer * 16 * 1024 / 8))

    uint8_t video_format = 5;
    uint8_t colour_primaries = 1;
    uint8_t transfer_characteristics = 1;
    uint8_t matrix_coefficients = 1;

    if (upipe_mpgvf->sequence_display != NULL) {
        size_t size;
        uint8_t display_buffer[MP2VSEQDX_HEADER_SIZE + MP2VSEQDX_COLOR_SIZE];
        const uint8_t *display;
        if (unlikely(!ubase_check(ubuf_block_size(upipe_mpgvf->sequence_display,
                                  &size)) ||
                     (display = ubuf_block_peek(upipe_mpgvf->sequence_display,
                        0,
                        size > MP2VSEQDX_HEADER_SIZE + MP2VSEQDX_COLOR_SIZE ?
                        MP2VSEQDX_HEADER_SIZE + MP2VSEQDX_COLOR_SIZE : size,
                        display_buffer)) == NULL)) {
            uref_free(flow_def);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }

        video_format = mp2vseqdx_get_format(display);
        colour_primaries = mp2vseqdx_get_primaries(display);
        transfer_characteristics = mp2vseqdx_get_transfer(display);
        matrix_coefficients = mp2vseqdx_get_matrixcoeffs(display);
        uint16_t display_horizontal = mp2vseqdx_get_horizontal(display);
        uint16_t display_vertical = mp2vseqdx_get_vertical(display);

        UBASE_FATAL(upipe, ubuf_block_peek_unmap(upipe_mpgvf->sequence_display,
                                                 0, display_buffer, display))
        UBASE_FATAL(upipe, uref_pic_flow_set_hsize_visible(flow_def,
                                                     display_horizontal))
        UBASE_FATAL(upipe, uref_pic_flow_set_vsize_visible(flow_def,
                                                     display_vertical))
    }

    const char *video_format_str = NULL;
    switch (video_format) {
        case 0:
            video_format_str = "component";
            break;
        case 1:
            video_format_str = "pal";
            break;
        case 2:
            video_format_str = "ntsc";
            break;
        case 3:
            video_format_str = "secam";
            break;
        case 4:
            video_format_str = "mac";
            break;
        default:
            break;
    }
    if (video_format_str != NULL) {
        UBASE_FATAL(upipe, uref_pic_flow_set_video_format(flow_def,
                    video_format_str))
    }

    const char *colour_primaries_str = NULL;
    switch (colour_primaries) {
        case 1:
            colour_primaries_str = "bt709";
            break;
        case 4:
            colour_primaries_str = "bt470m";
            break;
        case 5:
            colour_primaries_str = "bt470bg";
            break;
        case 6:
            colour_primaries_str = "smpte170m";
            break;
        case 7:
            colour_primaries_str = "smpte240m";
            break;
        default:
            break;
    }
    if (colour_primaries_str != NULL) {
        UBASE_FATAL(upipe, uref_pic_flow_set_colour_primaries(flow_def,
                    colour_primaries_str))
    }

    const char *transfer_characteristics_str = NULL;
    switch (transfer_characteristics) {
        case 1:
            transfer_characteristics_str = "bt709";
            break;
        case 4:
            transfer_characteristics_str = "bt470m";
            break;
        case 5:
            transfer_characteristics_str = "bt470bg";
            break;
        case 6:
            transfer_characteristics_str = "smpte170m";
            break;
        case 7:
            transfer_characteristics_str = "smpte240m";
            break;
        case 8:
            transfer_characteristics_str = "linear";
            break;
        default:
            break;
    }
    if (transfer_characteristics_str != NULL) {
        UBASE_FATAL(upipe, uref_pic_flow_set_transfer_characteristics(flow_def,
                    transfer_characteristics_str))
    }

    const char *matrix_coefficients_str = NULL;
    switch (matrix_coefficients) {
        case 1:
            matrix_coefficients_str = "bt709";
            break;
        case 4:
            matrix_coefficients_str = "fcc";
            break;
        case 5:
            matrix_coefficients_str = "bt470bg";
            break;
        case 6:
            matrix_coefficients_str = "smpte170m";
            break;
        case 7:
            matrix_coefficients_str = "smpte240m";
            break;
        default:
            break;
    }
    if (matrix_coefficients_str != NULL) {
        UBASE_FATAL(upipe, uref_pic_flow_set_matrix_coefficients(flow_def,
                    matrix_coefficients_str))
    }

    upipe_mpgvf_store_flow_def(upipe, NULL);
    uref_free(upipe_mpgvf->flow_def_requested);
    upipe_mpgvf->flow_def_requested = NULL;
    flow_def = upipe_mpgvf_store_flow_def_attr(upipe, flow_def);
    if (flow_def != NULL)
        upipe_mpgvf_require_flow_format(upipe, flow_def);

    return true;
}

/** @internal @This extracts the sequence header from a uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing a frame, beginning with a sequence header
 * @return pointer to ubuf containing only the sequence header
 */
static struct ubuf *upipe_mpgvf_extract_sequence(struct upipe *upipe,
                                                 struct uref *uref)
{
    uint8_t word;
    if (unlikely(!ubase_check(uref_block_extract(uref, 11, 1, &word))))
        return NULL;

    size_t sequence_header_size = MP2VSEQ_HEADER_SIZE;
    if (word & 0x2) {
        /* intra quantiser matrix */
        sequence_header_size += 64;
        if (unlikely(!ubase_check(uref_block_extract(uref, 11 + 64, 1, &word))))
            return NULL;
    }
    if (word & 0x1) {
        /* non-intra quantiser matrix */
        sequence_header_size += 64;
    }

    return ubuf_block_splice(uref->ubuf, 0, sequence_header_size);
}

/** @internal @This extracts the sequence extension from a uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing a frame, beginning with a sequence header
 * @param offset offset of the sequence extension in the uref
 * @return pointer to ubuf containing only the sequence extension
 */
static struct ubuf *upipe_mpgvf_extract_extension(struct upipe *upipe,
                                                  struct uref *uref,
                                                  size_t offset)
{
    return ubuf_block_splice(uref->ubuf, offset, MP2VSEQX_HEADER_SIZE);
}

/** @internal @This extracts the sequence display extension from a uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing a frame, beginning with a sequence header
 * @return pointer to ubuf containing only the sequence extension
 */
static struct ubuf *upipe_mpgvf_extract_display(struct upipe *upipe,
                                                struct uref *uref,
                                                size_t offset)
{
    uint8_t word;
    if (unlikely(!ubase_check(uref_block_extract(uref, offset + 4, 1, &word))))
        return NULL;
    return ubuf_block_splice(uref->ubuf, offset, MP2VSEQDX_HEADER_SIZE + 
                                   ((word & 0x1) ? MP2VSEQDX_COLOR_SIZE : 0));
}

/** @internal @This handles a uref containing a sequence header.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing a frame, beginning with a sequence header
 * @return false in case of error
 */
static bool upipe_mpgvf_handle_sequence(struct upipe *upipe, struct uref *uref)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    struct ubuf *sequence_ext = NULL;
    struct ubuf *sequence_display = NULL;
    struct ubuf *sequence_header = upipe_mpgvf_extract_sequence(upipe, uref);
    if (unlikely(sequence_header == NULL))
        return false;

    if (upipe_mpgvf->next_frame_sequence_ext_offset != -1) {
        sequence_ext = upipe_mpgvf_extract_extension(upipe, uref,
                upipe_mpgvf->next_frame_sequence_ext_offset);
        if (unlikely(sequence_ext == NULL)) {
            ubuf_free(sequence_header);
            return false;
        }

        if (upipe_mpgvf->next_frame_sequence_display_offset != -1) {
            sequence_display = upipe_mpgvf_extract_display(upipe, uref,
                    upipe_mpgvf->next_frame_sequence_display_offset);
            if (unlikely(sequence_display == NULL)) {
                ubuf_free(sequence_header);
                ubuf_free(sequence_ext);
                return false;
            }
        }
    }

    if (likely(upipe_mpgvf->sequence_header != NULL &&
               ubase_check(ubuf_block_equal(sequence_header,
                                  upipe_mpgvf->sequence_header)) &&
               ((upipe_mpgvf->sequence_ext == NULL && sequence_ext == NULL) ||
                (upipe_mpgvf->sequence_ext != NULL && sequence_ext != NULL &&
                 ubase_check(ubuf_block_equal(sequence_ext,
                                  upipe_mpgvf->sequence_ext)))) &&
               ((upipe_mpgvf->sequence_display == NULL &&
                 sequence_display == NULL) ||
                (upipe_mpgvf->sequence_display != NULL &&
                 sequence_display != NULL &&
                 ubase_check(ubuf_block_equal(sequence_display,
                                  upipe_mpgvf->sequence_display)))))) {
        /* identical sequence header, extension and display, but we rotate them
         * to free older buffers */
        ubuf_free(upipe_mpgvf->sequence_header);
        if (upipe_mpgvf->sequence_ext != NULL)
            ubuf_free(upipe_mpgvf->sequence_ext);
        if (upipe_mpgvf->sequence_display != NULL)
            ubuf_free(upipe_mpgvf->sequence_display);
        upipe_mpgvf->sequence_header = sequence_header;
        upipe_mpgvf->sequence_ext = sequence_ext;
        upipe_mpgvf->sequence_display = sequence_display;
        return true;
    }

    if (upipe_mpgvf->sequence_header != NULL)
        ubuf_free(upipe_mpgvf->sequence_header);
    if (upipe_mpgvf->sequence_ext != NULL)
        ubuf_free(upipe_mpgvf->sequence_ext);
    if (upipe_mpgvf->sequence_display != NULL)
        ubuf_free(upipe_mpgvf->sequence_display);
    upipe_mpgvf->sequence_header = sequence_header;
    upipe_mpgvf->sequence_ext = sequence_ext;
    upipe_mpgvf->sequence_display = sequence_display;

    return upipe_mpgvf_parse_sequence(upipe);
}

/** @internal @This parses a new picture header, and outputs a flow
 * definition
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing a frame
 * @param duration_p filled with duration
 * @return false in case of error
 */
static bool upipe_mpgvf_parse_picture(struct upipe *upipe, struct uref *uref,
                                      uint64_t *duration_p)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    upipe_mpgvf->closed_gop = false;
    bool brokenlink = false;
    if (upipe_mpgvf->next_frame_gop_offset != -1) {
        uint8_t gop_buffer[MP2VGOP_HEADER_SIZE];
        const uint8_t *gop;
        if (unlikely((gop = uref_block_peek(uref,
                                            upipe_mpgvf->next_frame_gop_offset,
                                            MP2VGOP_HEADER_SIZE,
                                            gop_buffer)) == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }
        upipe_mpgvf->closed_gop = mp2vgop_get_closedgop(gop);
        brokenlink = mp2vgop_get_brokenlink(gop);
        UBASE_FATAL(upipe, uref_block_peek_unmap(uref,
                                          upipe_mpgvf->next_frame_gop_offset,
                                          gop_buffer, gop))
        upipe_mpgvf->last_temporal_reference = -1;
        if (upipe_mpgvf->next_frame_gop_offset)
            uref_block_set_header_size(uref,
                                       upipe_mpgvf->next_frame_gop_offset);
    } else if (upipe_mpgvf->next_frame_offset)
        uref_block_set_header_size(uref, upipe_mpgvf->next_frame_offset);

    if ((brokenlink ||
        (!upipe_mpgvf->closed_gop && upipe_mpgvf->got_discontinuity)))
        uref_flow_set_discontinuity(uref);
    upipe_mpgvf->got_discontinuity = false;

    uint8_t picture_buffer[MP2VPIC_HEADER_SIZE];
    const uint8_t *picture;
    if (unlikely((picture = uref_block_peek(uref,
                                            upipe_mpgvf->next_frame_offset,
                                            MP2VPIC_HEADER_SIZE,
                                            picture_buffer)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    uint16_t temporalreference = mp2vpic_get_temporalreference(picture);
    uint8_t codingtype = mp2vpic_get_codingtype(picture);
    UBASE_FATAL(upipe, uref_block_peek_unmap(uref, upipe_mpgvf->next_frame_offset,
                                      picture_buffer, picture))

    uint64_t picture_number = upipe_mpgvf->last_picture_number +
        (temporalreference - upipe_mpgvf->last_temporal_reference);
    if (temporalreference > upipe_mpgvf->last_temporal_reference) {
        upipe_mpgvf->last_temporal_reference = temporalreference;
        upipe_mpgvf->last_picture_number = picture_number;
    }
    UBASE_FATAL(upipe, uref_pic_set_number(uref, picture_number))
    UBASE_FATAL(upipe, uref_mpgv_set_type(uref, codingtype))

    if (upipe_mpgvf->fps.num)
        *duration_p = UCLOCK_FREQ * upipe_mpgvf->fps.den / upipe_mpgvf->fps.num;
    else
        *duration_p = 0;

    if (upipe_mpgvf->next_frame_ext_offset != -1) {
        uint8_t ext_buffer[MP2VPICX_HEADER_SIZE];
        const uint8_t *ext;
        if (unlikely((ext = uref_block_peek(uref,
                                            upipe_mpgvf->next_frame_ext_offset,
                                            MP2VPICX_HEADER_SIZE,
                                            ext_buffer)) == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }
        uint8_t structure = mp2vpicx_get_structure(ext);
        bool tff = mp2vpicx_get_tff(ext);
        bool rff = mp2vpicx_get_rff(ext);
        bool progressive = mp2vpicx_get_progressive(ext);
        UBASE_FATAL(upipe, uref_block_peek_unmap(uref,
                                          upipe_mpgvf->next_frame_ext_offset,
                                          ext_buffer, ext))

        if (upipe_mpgvf->progressive_sequence) {
            if (rff)
                *duration_p *= 1 + tff;
        } else {
            if (structure == MP2VPICX_FRAME_PICTURE) {
                if (rff)
                    *duration_p += *duration_p / 2;
            } else
                *duration_p /= 2;
        }

        if (structure & MP2VPICX_TOP_FIELD)
            UBASE_FATAL(upipe, uref_pic_set_tf(uref))
        if (structure & MP2VPICX_BOTTOM_FIELD)
            UBASE_FATAL(upipe, uref_pic_set_bf(uref))
        if (tff)
            UBASE_FATAL(upipe, uref_pic_set_tff(uref))
        if (progressive)
            UBASE_FATAL(upipe, uref_pic_set_progressive(uref))
    } else {
        UBASE_FATAL(upipe, uref_pic_set_tf(uref))
        UBASE_FATAL(upipe, uref_pic_set_bf(uref))
        UBASE_FATAL(upipe, uref_pic_set_progressive(uref))
    }

    UBASE_FATAL(upipe, uref_clock_set_duration(uref, *duration_p))

    return true;
}

/** @internal @This handles a uref containing a picture header.
 *
 * @param upipe description structure of the pipe
 * @param uref uref containing a frame
 * @param duration_p filled with the duration
 * @return false in case of error
 */
static bool upipe_mpgvf_handle_picture(struct upipe *upipe, struct uref *uref,
                                       uint64_t *duration_p)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    if (unlikely(!upipe_mpgvf_parse_picture(upipe, uref, duration_p)))
        return false;

    uint8_t type;
    if (!ubase_check(uref_mpgv_get_type(uref, &type)))
        return false;

    switch (type) {
        case MP2VPIC_TYPE_I: {
            if (upipe_mpgvf->next_frame_sequence)
                uref_flow_set_random(uref);
            UBASE_FATAL(upipe, uref_pic_set_key(uref))

            upipe_mpgvf->ref_rap = upipe_mpgvf->iframe_rap;
            upipe_mpgvf->iframe_rap = upipe_mpgvf->seq_rap;
            if (upipe_mpgvf->iframe_rap != UINT64_MAX)
                uref_clock_set_rap_sys(uref, upipe_mpgvf->iframe_rap);
            break;
        }

        case MP2VPIC_TYPE_P:
            upipe_mpgvf->ref_rap = upipe_mpgvf->iframe_rap;
            if (upipe_mpgvf->iframe_rap != UINT64_MAX)
                uref_clock_set_rap_sys(uref, upipe_mpgvf->iframe_rap);
            break;

        case MP2VPIC_TYPE_B:
            if (upipe_mpgvf->ref_rap != UINT64_MAX)
                uref_clock_set_rap_sys(uref, upipe_mpgvf->ref_rap);
            break;
    }

    if (upipe_mpgvf->closed_gop)
        upipe_mpgvf->ref_rap = upipe_mpgvf->iframe_rap;
    return true;
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgvf_build_flow_def(struct upipe *upipe)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    if (upipe_mpgvf->flow_def_requested == NULL)
        return;

    struct uref *flow_def = uref_dup(upipe_mpgvf->flow_def_requested);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (upipe_mpgvf->fps.num) {
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                    upipe_mpgvf->input_latency +
                    UCLOCK_FREQ * upipe_mpgvf->fps.den / upipe_mpgvf->fps.num))
    }

    upipe_mpgvf->sequence_requested =
        !ubase_check(uref_mpgv_flow_get_repeated_sequence(
                    upipe_mpgvf->flow_def_requested));

    upipe_mpgvf_store_flow_def(upipe, flow_def);
}

/** @internal @This handles and outputs a frame.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 * @return false if the stream needs to be resync'd
 */
static bool upipe_mpgvf_output_frame(struct upipe *upipe,
                                     struct upump **upump_p)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    struct uref *uref = NULL;

    if (unlikely(upipe_mpgvf->sequence_header == NULL &&
                 !upipe_mpgvf->next_frame_sequence)) {
        upipe_mpgvf_consume_uref_stream(upipe, upipe_mpgvf->next_frame_size);
        return true;
    }

    /* The PTS can be updated up to the first octet of the picture start code,
     * so any preceding structure must be extracted before, so that the PTS
     * can be properly promoted and taken into account. */
    if (upipe_mpgvf->next_frame_offset) {
        uref = upipe_mpgvf_extract_uref_stream(upipe,
                upipe_mpgvf->next_frame_offset);
        if (unlikely(uref == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }
    }

    struct uref au_uref_s = upipe_mpgvf->au_uref_s;
    struct urational drift_rate = upipe_mpgvf->drift_rate;
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_mpgvf_flush_dates(upipe);

    struct uref *uref2 = upipe_mpgvf_extract_uref_stream(upipe,
            upipe_mpgvf->next_frame_size - upipe_mpgvf->next_frame_offset);
    if (unlikely(uref2 == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }
    if (uref != NULL) {
        uref_block_append(uref, uref_detach_ubuf(uref2));
        uref_free(uref2);
    } else
        uref = uref2;

    if (upipe_mpgvf->next_frame_sequence) {
        if (unlikely(!upipe_mpgvf_handle_sequence(upipe, uref))) {
            uref_free(uref);
            return false;
        }
    }

    uint64_t duration;
    if (unlikely(!upipe_mpgvf_handle_picture(upipe, uref, &duration))) {
        uref_free(uref);
        return false;
    }

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(&au_uref_s, &date))) {          \
        uref_clock_set_dts_##dv(uref, date);                                \
        if (!ubase_check(uref_clock_get_dts_##dv(&upipe_mpgvf->au_uref_s,   \
                                                 NULL)))                    \
            uref_clock_set_dts_##dv(&upipe_mpgvf->au_uref_s,                \
                                    date + duration);                       \
    } else if (ubase_check(uref_clock_get_dts_##dv(uref, &date)))           \
        uref_clock_set_date_##dv(uref, UINT64_MAX, UREF_DATE_NONE);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    if (ubase_check(uref_clock_get_dts_pts_delay(&au_uref_s, &date)))
        uref_clock_set_dts_pts_delay(uref, date);
    else
        uref_clock_delete_dts_pts_delay(uref);
    if (drift_rate.den)
        uref_clock_set_rate(uref, drift_rate);
    else
        uref_clock_delete_rate(uref);

    if (unlikely(upipe_mpgvf->flow_def == NULL))
        upipe_mpgvf_build_flow_def(upipe);

    if (unlikely(upipe_mpgvf->flow_def == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    /* Force sending and possibly negotiating flow def before changing
     * bitstream. */
    upipe_mpgvf_output(upipe, NULL, upump_p);

    if (upipe_mpgvf->sequence_requested &&
        ubase_check(uref_pic_get_key(uref)) &&
        !ubase_check(uref_flow_get_random(uref))) {
        struct ubuf *ubuf;
        if (upipe_mpgvf->sequence_display != NULL) {
            ubuf = ubuf_dup(upipe_mpgvf->sequence_display);
            if (unlikely(ubuf == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return false;
            }
            uref_block_insert(uref, 0, ubuf);
        }
        if (upipe_mpgvf->sequence_ext != NULL) {
            ubuf = ubuf_dup(upipe_mpgvf->sequence_ext);
            if (unlikely(ubuf == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return false;
            }
            uref_block_insert(uref, 0, ubuf);
        }
        ubuf = ubuf_dup(upipe_mpgvf->sequence_header);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }
        uref_block_insert(uref, 0, ubuf);
        uref_flow_set_random(uref);
    }

    upipe_mpgvf_output(upipe, uref, upump_p);
    return true;
}

/** @internal @This is called back by @ref upipe_mpgvf_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgvf_promote_uref(struct upipe *upipe)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(upipe_mpgvf->next_uref, &date)))\
        uref_clock_set_dts_##dv(&upipe_mpgvf->au_uref_s, date);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    if (ubase_check(uref_clock_get_dts_pts_delay(upipe_mpgvf->next_uref,
                                                 &date)))
        uref_clock_set_dts_pts_delay(&upipe_mpgvf->au_uref_s, date);
    uref_clock_get_rate(upipe_mpgvf->next_uref, &upipe_mpgvf->drift_rate);
    if (ubase_check(uref_clock_get_dts_prog(upipe_mpgvf->next_uref, &date)))
        uref_clock_get_rap_sys(upipe_mpgvf->next_uref, &upipe_mpgvf->dts_rap);
}

/** @internal @This resets the internal parsing state.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgvf_reset(struct upipe *upipe)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    upipe_mpgvf->next_frame_sequence = false;
    upipe_mpgvf->next_frame_sequence_ext_offset = -1;
    upipe_mpgvf->next_frame_sequence_display_offset = -1;
    upipe_mpgvf->next_frame_gop_offset = -1;
    upipe_mpgvf->next_frame_offset = -1;
    upipe_mpgvf->next_frame_ext_offset = -1;
    upipe_mpgvf->next_frame_slice = false;
}

/** @internal @This tries to output frames from the queue of input buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_mpgvf_work(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    while (upipe_mpgvf->next_uref != NULL) {
        uint8_t start, next;
        if (!upipe_mpgvf_find(upipe, &start, &next))
            return;

        if (unlikely(!upipe_mpgvf->acquired)) {
            upipe_mpgvf_consume_uref_stream(upipe,
                                            upipe_mpgvf->next_frame_size - 4);
            upipe_mpgvf->next_frame_size = 4;

            switch (start) {
                case MP2VPIC_START_CODE:
                    upipe_mpgvf_flush_dates(upipe);
                    break;
                case MP2VSEQ_START_CODE:
                    upipe_mpgvf_sync_acquired(upipe);
                    upipe_mpgvf->next_frame_sequence = true;
                    break;
                default:
                    break;
            }
            continue;
        }

        if (unlikely(upipe_mpgvf->next_frame_offset == -1)) {
            if (start == MP2VX_START_CODE) {
                if (mp2vxst_get_id(next) == MP2VX_ID_SEQX)
                    upipe_mpgvf->next_frame_sequence_ext_offset =
                        upipe_mpgvf->next_frame_size - 4;
                else if (mp2vxst_get_id(next) == MP2VX_ID_SEQDX)
                    upipe_mpgvf->next_frame_sequence_display_offset =
                        upipe_mpgvf->next_frame_size - 4;
            } else if (start == MP2VGOP_START_CODE)
                upipe_mpgvf->next_frame_gop_offset =
                    upipe_mpgvf->next_frame_size - 4;
            else if (start == MP2VPIC_START_CODE)
                upipe_mpgvf->next_frame_offset =
                    upipe_mpgvf->next_frame_size - 4;
            continue;
        }

        if (start == MP2VX_START_CODE) {
            if (mp2vxst_get_id(next) == MP2VX_ID_PICX)
                upipe_mpgvf->next_frame_ext_offset =
                    upipe_mpgvf->next_frame_size - 4;
            continue;
        }

        if (start == MP2VUSR_START_CODE)
            continue;

        if (start > MP2VPIC_START_CODE && start <= MP2VPIC_LAST_CODE) {
            /* slice header */
            upipe_mpgvf->next_frame_slice = true;
            continue;
        }

        if (start != MP2VEND_START_CODE)
            upipe_mpgvf->next_frame_size -= 4;

        if (unlikely(!upipe_mpgvf_output_frame(upipe, upump_p))) {
            upipe_warn(upipe, "erroneous frame headers");
            upipe_mpgvf->next_frame_size = 0;
            upipe_mpgvf->scan_context = UINT32_MAX;
            upipe_mpgvf_sync_lost(upipe);
            upipe_mpgvf_reset(upipe);
            continue;
        }
        upipe_mpgvf_reset(upipe);
        upipe_mpgvf->next_frame_size = 4;

        switch (start) {
            case MP2VSEQ_START_CODE:
                upipe_mpgvf->next_frame_sequence = true;
                upipe_mpgvf->seq_rap = upipe_mpgvf->dts_rap;
                break;
            case MP2VGOP_START_CODE:
                upipe_mpgvf->next_frame_gop_offset = 0;
                break;
            case MP2VPIC_START_CODE:
                upipe_mpgvf->next_frame_offset = 0;
                break;
            case MP2VEND_START_CODE:
                upipe_mpgvf->next_frame_size = 0;
                upipe_mpgvf_sync_lost(upipe);
                break;
            default:
                upipe_warn_va(upipe, "erroneous start code %x", start);
                upipe_mpgvf_sync_lost(upipe);
                break;
        }
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_mpgvf_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_mpgvf->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_mpgvf->input_latency);
        upipe_mpgvf_store_flow_def(upipe, NULL);
        uref_free(upipe_mpgvf->flow_def_requested);
        upipe_mpgvf->flow_def_requested = NULL;
        uref = upipe_mpgvf_store_flow_def_input(upipe, uref);
        if (uref != NULL)
            upipe_mpgvf_require_flow_format(upipe, uref);
        return true;
    }

    if (upipe_mpgvf->flow_def_requested == NULL &&
        upipe_mpgvf->flow_def_attr != NULL)
        return false;

    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref)))) {
        if (!upipe_mpgvf->next_frame_slice) {
            /* we do not want discontinuities in the headers before the first
             * slice header; inside the slices it is less destructive */
            upipe_mpgvf_clean_uref_stream(upipe);
            upipe_mpgvf_init_uref_stream(upipe);
            upipe_mpgvf->got_discontinuity = true;
            upipe_mpgvf->next_frame_size = 0;
            upipe_mpgvf->scan_context = UINT32_MAX;
            upipe_mpgvf_sync_lost(upipe);
            upipe_mpgvf_reset(upipe);
        } else
            uref_flow_set_error(upipe_mpgvf->next_uref);
    }

    upipe_mpgvf_append_uref_stream(upipe, uref);
    upipe_mpgvf_work(upipe, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_mpgvf_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    if (!upipe_mpgvf_check_input(upipe)) {
        upipe_mpgvf_hold_input(upipe, uref);
        upipe_mpgvf_block_input(upipe, upump_p);
    } else if (!upipe_mpgvf_handle(upipe, uref, upump_p)) {
        upipe_mpgvf_hold_input(upipe, uref);
        upipe_mpgvf_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives the result of a flow format request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_mpgvf_check_flow_format(struct upipe *upipe,
                                         struct uref *flow_format)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    uref_free(upipe_mpgvf->flow_def_requested);
    upipe_mpgvf->flow_def_requested = flow_format;
    upipe_mpgvf_build_flow_def(upipe);

    bool was_buffered = !upipe_mpgvf_check_input(upipe);
    upipe_mpgvf_output_input(upipe);
    upipe_mpgvf_unblock_input(upipe);
    if (was_buffered && upipe_mpgvf_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_mpgvf_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_mpgvf_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.mpeg1video.") &&
                  ubase_ncmp(def, "block.mpeg2video.") &&
                  strcmp(def, "block."))))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a mpgvf pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_mpgvf_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_mpgvf_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_mpgvf_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_mpgvf_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_mpgvf_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_mpgvf_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_mpgvf_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_mpgvf_free(struct upipe *upipe)
{
    struct upipe_mpgvf *upipe_mpgvf = upipe_mpgvf_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_mpgvf_clean_uref_stream(upipe);
    upipe_mpgvf_clean_input(upipe);
    upipe_mpgvf_clean_output(upipe);
    uref_free(upipe_mpgvf->flow_def_requested);
    upipe_mpgvf_clean_flow_format(upipe);
    upipe_mpgvf_clean_flow_def(upipe);
    upipe_mpgvf_clean_sync(upipe);

    if (upipe_mpgvf->sequence_header != NULL)
        ubuf_free(upipe_mpgvf->sequence_header);
    if (upipe_mpgvf->sequence_ext != NULL)
        ubuf_free(upipe_mpgvf->sequence_ext);
    if (upipe_mpgvf->sequence_display != NULL)
        ubuf_free(upipe_mpgvf->sequence_display);

    upipe_mpgvf_clean_urefcount(upipe);
    upipe_mpgvf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_mpgvf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_MPGVF_SIGNATURE,

    .upipe_alloc = upipe_mpgvf_alloc,
    .upipe_input = upipe_mpgvf_input,
    .upipe_control = upipe_mpgvf_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all mpgvf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mpgvf_mgr_alloc(void)
{
    return &upipe_mpgvf_mgr;
}

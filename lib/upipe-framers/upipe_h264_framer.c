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
#include <upipe/ubuf_block_stream.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/uref_h264_flow.h>

#include "upipe_framers_common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/h264.h>

/** @internal @This translates the h264 aspect_ratio_idc to urational */
static const struct urational sar_from_idc[] = {
    { .num = 1, .den = 1 }, /* unspecified - treat as square */
    { .num = 1, .den = 1 },
    { .num = 12, .den = 11 },
    { .num = 10, .den = 11 },
    { .num = 16, .den = 11 },
    { .num = 40, .den = 33 },
    { .num = 24, .den = 11 },
    { .num = 20, .den = 11 },
    { .num = 32, .den = 11 },
    { .num = 80, .den = 33 },
    { .num = 18, .den = 11 },
    { .num = 15, .den = 11 },
    { .num = 64, .den = 33 },
    { .num = 160, .den = 99 },
    { .num = 4, .den = 3 },
    { .num = 3, .den = 2 },
    { .num = 2, .den = 1 },
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
    /** last frame num read from the stream, or -1 */
    int32_t last_frame_num;
    /** true we have had a discontinuity recently */
    bool got_discontinuity;
    /** pointers to sequence parameter sets */
    struct ubuf *sps[H264SPS_ID_MAX];
    /** pointers to sequence parameter set extensions */
    struct ubuf *sps_ext[H264SPS_ID_MAX];
    /** active sequence parameter set, or -1 */
    int active_sps;
    /** pointers to picture parameter sets */
    struct ubuf *pps[H264PPS_ID_MAX];
    /** active picture parameter set, or -1 */
    int active_pps;

    /* parsing results - headers */
    /** length of frame number field */
    uint32_t log2_max_frame_num;
    /** pic order cnt type */
    uint32_t poc_type;
    /** length of poc lsb field */
    uint32_t log2_max_poc_lsb;
    /** delta poc always zero field */
    bool delta_poc_always_zero;
    /** separate color plane */
    bool separate_colour_plane;
    /** frame mbs only field */
    bool frame_mbs_only;
    /** time scale */
    uint32_t time_scale;
    /** true if hrd structures are present */
    bool hrd;
    /** length of the field */
    uint8_t initial_cpb_removal_delay_length;
    /** length of the field */
    uint8_t cpb_removal_delay_length;
    /** length of the field */
    uint8_t dpb_output_delay_length;
    /** octet rate */
    uint64_t octet_rate;
    /** CPB length in clock units */
    uint64_t cpb_length;
    /** duration of a frame */
    uint64_t duration;
    /** true if picture structure is present */
    bool pic_struct_present;
    /** true if bottom_field_pic_order_in_frame is present */
    bool bf_poc;
    /** sample aspect ratio */
    struct urational sar;

    /* parsing results - slice */
    /** field */
    int64_t initial_cpb_removal_delay;
    /** picture structure */
    int pic_struct;
    /** frame number */
    uint32_t frame_num;
    /** field pic */
    bool field_pic;
    /** bottom field */
    bool bf;
    /** IDR pic ID */
    uint32_t idr_pic_id;
    /** picture order count lsb (poc_type == 0) */
    uint32_t poc_lsb;
    /** delta picture order count bottom (poc_type == 0) */
    int32_t delta_poc_bottom;
    /** delta picture order count 0 (poc_type == 1) */
    int32_t delta_poc0;
    /** delta picture order count 1 (poc_type == 1) */
    int32_t delta_poc1;

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
    /** true if there is already a slice starting in this access unit */
    bool au_slice;
    /** NAL start code of the first slice, or UINT8_MAX */
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

UPIPE_HELPER_UPIPE(upipe_h264f, upipe, UPIPE_H264F_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_h264f, UPIPE_H264F_EXPECTED_FLOW_DEF)
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
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_h264f_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_h264f_alloc_flow(mgr, uprobe, signature, args,
                                                 &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_h264f_init_sync(upipe);
    upipe_h264f_init_uref_stream(upipe);
    upipe_h264f_init_output(upipe);
    upipe_h264f->flow_def_input = flow_def;
    upipe_h264f->systime_rap = UINT64_MAX;
    upipe_h264f->last_picture_number = 0;
    upipe_h264f->last_frame_num = -1;
    upipe_h264f->initial_cpb_removal_delay = INT64_MAX;
    upipe_h264f->pic_struct = -1;
    upipe_h264f->got_discontinuity = false;
    upipe_h264f->scan_context = UINT32_MAX;
    upipe_h264f->au_size = 0;
    upipe_h264f->au_last_nal_offset = -1;
    upipe_h264f->au_last_nal = UINT8_MAX;
    upipe_h264f->au_last_nal_start_size = 0;
    upipe_h264f->au_vcl_offset = -1;
    upipe_h264f->au_slice = false;
    upipe_h264f->au_slice_nal = UINT8_MAX;
    upipe_h264f_flush_pts(upipe);
    upipe_h264f_flush_dts(upipe);

    int i;
    for (i = 0; i < H264SPS_ID_MAX; i++) {
        upipe_h264f->sps[i] = NULL;
        upipe_h264f->sps_ext[i] = NULL;
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

/** @internal @This allows to skip escape words from NAL units. */
struct upipe_h264f_stream {
    /** positions of the 0s in the previous octets */
    uint8_t zeros;
    /** standard octet stream */
    struct ubuf_block_stream s;
};

/** @internal @This initializes the helper structure for octet stream.
 *
 * @param f helper structure
 */
static inline void upipe_h264f_stream_init(struct upipe_h264f_stream *f)
{
    f->zeros = 0;
}

/** @internal @This gets the next octet in the ubuf while bypassing escape
 * words.
 *
 * @param s helper structure
 * @param ubuf pointer to block ubuf
 * @param offset start offset
 * @return false in case of error
 */
static inline bool upipe_h264f_stream_get(struct ubuf_block_stream *s,
                                          uint8_t *octet_p)
{
    if (unlikely(!ubuf_block_stream_get(s, octet_p)))
        return false;
    struct upipe_h264f_stream *f =
        container_of(s, struct upipe_h264f_stream, s);
    f->zeros <<= 1;
    if (unlikely(!*octet_p))
        f->zeros |= 1;
    else if (unlikely(*octet_p == 3 && (f->zeros & 6) == 6)) /* escape word */
        return ubuf_block_stream_get(s, octet_p);
    return true;
}

/** @This fills the bit stream cache with at least the given number of bits.
 *
 * @param s helper structure
 * @param nb number of bits to ensure
 */
#define upipe_h264f_stream_fill_bits(s, nb)                                 \
    ubuf_block_stream_fill_bits_inner(s, upipe_h264f_stream_get, nb)

/** @internal @This reads an unsigned exp-golomb code from a stream.
 *
 * @param s ubuf block stream
 * @return code read
 */
static uint32_t upipe_h264f_stream_ue(struct ubuf_block_stream *s)
{
    int i = 1;
    while (i < 32) {
        upipe_h264f_stream_fill_bits(s, 8);
        uint8_t octet = ubuf_block_stream_show_bits(s, 8);
        if (likely(octet))
            break;
        i += 8;
        ubuf_block_stream_skip_bits(s, 8);
    }
    while (i < 32 && !ubuf_block_stream_show_bits(s, 1)) {
        i++;
        ubuf_block_stream_skip_bits(s, 1);
    }

    if (likely(i <= 24)) {
        upipe_h264f_stream_fill_bits(s, i);
        uint32_t result = ubuf_block_stream_show_bits(s, i);
        ubuf_block_stream_skip_bits(s, i);
        return result - 1;
    }

    upipe_h264f_stream_fill_bits(s, 8);
    uint32_t result = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    i -= 8;
    result <<= i;
    upipe_h264f_stream_fill_bits(s, i);
    result += ubuf_block_stream_show_bits(s, i);
    ubuf_block_stream_skip_bits(s, i);
    return result - 1;
}

/** @internal @This reads a signed exp-golomb code from a stream.
 *
 * @param s ubuf block stream
 * @return code read
 */
static int32_t upipe_h264f_stream_se(struct ubuf_block_stream *s)
{
    uint32_t v = upipe_h264f_stream_ue(s);

    return (v & 1) ? (v + 1) / 2 : -(v / 2);
}

/** @internal @This parses scaling matrices.
 *
 * @param s ubuf block stream
 */
static void upipe_h264f_stream_parse_scaling(struct ubuf_block_stream *s,
                                             int nb_lists)
{
    int i, j;
    for (i = 0; i < nb_lists; i++) {
        upipe_h264f_stream_fill_bits(s, 1);
        bool seq_scaling_list = ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (!seq_scaling_list)
            continue;

        int scaling_list_size = (i < 6) ? 16 : 64;
        int32_t last_scale = 8, next_scale = 8;
        for (j = 0; j < scaling_list_size; j++) {
            if (next_scale != 0)
                next_scale = (last_scale + upipe_h264f_stream_se(s) + 256) %
                             256;
            last_scale = (next_scale == 0) ? last_scale : next_scale;
        }
    }
}

/** @internal @This parses hrd parameters.
 *
 * @param upipe description structure of the pipe
 * @param s ubuf block stream
 * @param octetrate_p filled in with the octet rate
 * @param cpb_size_p fille in with the CPB buffer size
 * @return true if the stream is CBR
 */
static bool upipe_h264f_stream_parse_hrd(struct upipe *upipe,
                                         struct ubuf_block_stream *s,
                                         uint64_t *octetrate_p,
                                         uint64_t *cpb_size_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    bool ret = false;
    uint32_t cpb_cnt = upipe_h264f_stream_ue(s) + 1;
    upipe_h264f_stream_fill_bits(s, 8);
    uint8_t bitrate_scale = ubuf_block_stream_show_bits(s, 4);
    ubuf_block_stream_skip_bits(s, 4);
    uint8_t cpb_size_scale = ubuf_block_stream_show_bits(s, 4);
    ubuf_block_stream_skip_bits(s, 4);
    uint64_t octetrate =
        ((upipe_h264f_stream_ue(s) + 1) << (6 + bitrate_scale)) / 8;
    uint64_t cpb_size =
        ((upipe_h264f_stream_ue(s) + 1) << (4 + cpb_size_scale)) / 8;
    upipe_h264f_stream_fill_bits(s, 1);
    if (ubuf_block_stream_show_bits(s, 1)) { /* cbr_flag */
        *octetrate_p = octetrate;
        *cpb_size_p = cpb_size;
        ret = true;
    }
    ubuf_block_stream_skip_bits(s, 1);
    cpb_cnt--;
    while (cpb_cnt) {
        upipe_h264f_stream_ue(s);
        upipe_h264f_stream_ue(s);
        upipe_h264f_stream_fill_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        cpb_cnt--;
    }

    upipe_h264f_stream_fill_bits(s, 20);
    upipe_h264f->initial_cpb_removal_delay_length =
        ubuf_block_stream_show_bits(s, 5) + 1;
    ubuf_block_stream_skip_bits(s, 5);
    upipe_h264f->cpb_removal_delay_length =
        ubuf_block_stream_show_bits(s, 5) + 1;
    ubuf_block_stream_skip_bits(s, 5);
    upipe_h264f->dpb_output_delay_length =
        ubuf_block_stream_show_bits(s, 5) + 1;
    ubuf_block_stream_skip_bits(s, 10);
    return ret;
}

/** @internal @This handles a sequence parameter set.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sps(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->au_last_nal_start_size != 5)
        return;

    struct ubuf *ubuf = ubuf_block_splice(upipe_h264f->next_uref->ubuf,
                                          upipe_h264f->au_last_nal_offset,
                                          upipe_h264f->au_size -
                                            upipe_h264f->au_last_nal_offset);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    struct upipe_h264f_stream f;
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, ubuf,
                                      upipe_h264f->au_last_nal_start_size +
                                      H264SPS_HEADER_SIZE - 4);
    if (unlikely(!ret)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    uint32_t sps_id = upipe_h264f_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32, sps_id);
        ubuf_free(ubuf);
        return;
    }

    if (upipe_h264f->active_sps == sps_id &&
        !ubuf_block_equal(upipe_h264f->sps[sps_id], ubuf))
        upipe_h264f->active_sps = -1;

    if (upipe_h264f->sps[sps_id] != NULL)
        ubuf_free(upipe_h264f->sps[sps_id]);
    upipe_h264f->sps[sps_id] = ubuf;
}

/** @internal @This handles a sequence parameter set extension.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sps_ext(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->au_last_nal_start_size != 5)
        return;

    struct ubuf *ubuf = ubuf_block_splice(upipe_h264f->next_uref->ubuf,
                                          upipe_h264f->au_last_nal_offset,
                                          upipe_h264f->au_size -
                                            upipe_h264f->au_last_nal_offset);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    struct upipe_h264f_stream f;
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, ubuf,
                                      upipe_h264f->au_last_nal_start_size);
    assert(ret);
    uint32_t sps_id = upipe_h264f_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS extension %"PRIu32, sps_id);
        ubuf_free(ubuf);
        return;
    }

    /* We do not reset active_sps because so far we don't care about SPS
     * extension. */

    if (upipe_h264f->sps_ext[sps_id] != NULL)
        ubuf_free(upipe_h264f->sps[sps_id]);
    upipe_h264f->sps[sps_id] = ubuf;
}

/** @internal @This handles a picture parameter set.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_pps(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->au_last_nal_start_size != 5)
        return;

    struct ubuf *ubuf = ubuf_block_splice(upipe_h264f->next_uref->ubuf,
                                          upipe_h264f->au_last_nal_offset,
                                          upipe_h264f->au_size -
                                            upipe_h264f->au_last_nal_offset);
    if (unlikely(ubuf == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    struct upipe_h264f_stream f;
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, ubuf,
                                      upipe_h264f->au_last_nal_start_size);
    assert(ret);
    uint32_t pps_id = upipe_h264f_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(pps_id >= H264PPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid PPS %"PRIu32, pps_id);
        ubuf_free(ubuf);
        return;
    }

    if (upipe_h264f->active_pps == pps_id &&
        !ubuf_block_equal(upipe_h264f->pps[pps_id], ubuf))
        upipe_h264f->active_pps = -1;

    if (upipe_h264f->pps[pps_id] != NULL)
        ubuf_free(upipe_h264f->pps[pps_id]);
    upipe_h264f->pps[pps_id] = ubuf;
}

/** @internal @This activates a sequence parameter set.
 *
 * @param upipe description structure of the pipe
 * @param sps_id SPS to activate
 * @return false if the SPS couldn't be activated
 */
static bool upipe_h264f_activate_sps(struct upipe *upipe, uint32_t sps_id)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (likely(upipe_h264f->active_sps == sps_id))
        return true;
    if (unlikely(upipe_h264f->sps[sps_id] == NULL))
        return false;

    struct uref *flow_def = uref_dup(upipe_h264f->flow_def_input);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return false;
    }

    struct upipe_h264f_stream f;
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, upipe_h264f->sps[sps_id], 5);
    assert(ret);

    upipe_h264f_stream_fill_bits(s, 24);
    uint8_t profile = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 16);
    ret = ret && uref_h264_flow_set_profile(flow_def, profile);
    uint8_t level = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    ret = ret && uref_h264_flow_set_level(flow_def, level);

    uint64_t max_octetrate;
    switch (level) {
        case 10: max_octetrate = 64000 / 8; break;
        case 11: max_octetrate = 192000 / 8; break;
        case 12: max_octetrate = 384000 / 8; break;
        case 13: max_octetrate = 768000 / 8; break;
        case 20: max_octetrate = 2000000 / 8; break;
        case 21:
        case 22: max_octetrate = 4000000 / 8; break;
        case 30: max_octetrate = 10000000 / 8; break;
        case 31: max_octetrate = 14000000 / 8; break;
        case 32:
        case 40: max_octetrate = 20000000 / 8; break;
        case 41:
        case 42: max_octetrate = 50000000 / 8; break;
        case 50: max_octetrate = 135000000 / 8; break;
        default:
            upipe_warn_va(upipe, "unknown level %"PRIu8, level);
            /* intended fall-through */
        case 51: max_octetrate = 240000000 / 8; break;
    }
    ret = ret && uref_block_flow_set_max_octetrate(flow_def, max_octetrate);

    upipe_h264f_stream_ue(s); /* sps_id */
    uint32_t chroma_idc = 1;
    uint8_t luma_depth = 8, chroma_depth = 8;
    upipe_h264f->separate_colour_plane = false;;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile ==  44 || profile ==  83 || profile ==  86 || profile == 118 ||
        profile == 128)
    {
        chroma_idc = upipe_h264f_stream_ue(s);
        if (chroma_idc == H264SPS_CHROMA_444) {
            upipe_h264f_stream_fill_bits(s, 1);
            upipe_h264f->separate_colour_plane =
                !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }
        luma_depth += upipe_h264f_stream_ue(s);
        if (!upipe_h264f->separate_colour_plane)
            chroma_depth += upipe_h264f_stream_ue(s);
        else
            chroma_depth = luma_depth;
        upipe_h264f_stream_fill_bits(s, 2);
        ubuf_block_stream_skip_bits(s, 1); /* qpprime_y_zero_transform_etc. */
        bool seq_scaling_matrix = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);

        if (seq_scaling_matrix)
            upipe_h264f_stream_parse_scaling(s,
                    chroma_idc != H264SPS_CHROMA_444 ? 8 : 12);
    }

    ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
    ret = ret && uref_pic_flow_set_planes(flow_def, 0);
    if (luma_depth == 8)
        ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8");
    else {
        ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y16");
        luma_depth = 16;
    }
    if (chroma_idc == H264SPS_CHROMA_MONO)
        ret = ret && uref_flow_set_def_va(flow_def,
                UPIPE_H264F_EXPECTED_FLOW_DEF "pic.planar%"PRIu8"_mono.",
                luma_depth);
    else {
        uint8_t hsub, vsub;
        const char *chroma;
        switch (chroma_idc) {
            case H264SPS_CHROMA_420:
                hsub = 2;
                vsub = 2;
                chroma = "420";
                break;
            case H264SPS_CHROMA_422:
                hsub = 2;
                vsub = 1;
                chroma = "422";
                break;
            case H264SPS_CHROMA_444:
                hsub = 1;
                vsub = 1;
                chroma = "444";
                break;
            default:
                upipe_err_va(upipe, "invalid chroma format %"PRIu32,
                             chroma_idc);
                ubuf_block_stream_clean(s);
                uref_free(flow_def);
                return false;
        }
        if (chroma_depth == 8) {
            ret = ret && uref_pic_flow_add_plane(flow_def, hsub, vsub, 1, "u8");
            ret = ret && uref_pic_flow_add_plane(flow_def, hsub, vsub, 1, "v8");
            ret = ret && uref_flow_set_def_va(flow_def,
                    UPIPE_H264F_EXPECTED_FLOW_DEF "pic.planar%"PRIu8"_8_%s.",
                    luma_depth, chroma);
        } else {
            ret = ret && uref_pic_flow_add_plane(flow_def, hsub, vsub, 2,
                                                 "u16");
            ret = ret && uref_pic_flow_add_plane(flow_def, hsub, vsub, 2,
                                                 "v16");
            ret = ret && uref_flow_set_def_va(flow_def,
                    UPIPE_H264F_EXPECTED_FLOW_DEF "pic.planar%"PRIu8"_16_%s.",
                    luma_depth, chroma);
        }
    }

    /* Skip i_log2_max_frame_num */
    upipe_h264f->log2_max_frame_num = 4 + upipe_h264f_stream_ue(s);
    if (upipe_h264f->log2_max_frame_num > 16) {
        upipe_err_va(upipe, "invalid log2_max_frame_num %"PRIu32,
                     upipe_h264f->log2_max_frame_num);
        upipe_h264f->log2_max_frame_num = 0;
        ubuf_block_stream_clean(s);
        uref_free(flow_def);
        return false;
    }

    upipe_h264f->poc_type = upipe_h264f_stream_ue(s);
    if (!upipe_h264f->poc_type) {
        upipe_h264f->log2_max_poc_lsb = 4 + upipe_h264f_stream_ue(s);
        if (upipe_h264f->log2_max_poc_lsb > 16) {
            upipe_err_va(upipe, "invalid log2_max_frame_num %"PRIu32,
                         upipe_h264f->log2_max_frame_num);
            upipe_h264f->log2_max_poc_lsb = 0;
            ubuf_block_stream_clean(s);
            uref_free(flow_def);
            return false;
        }

    } else if (upipe_h264f->poc_type == 1) {
        upipe_h264f_stream_fill_bits(s, 1);
        upipe_h264f->delta_poc_always_zero =
            !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        upipe_h264f_stream_se(s); /* offset_for_non_ref_pic */
        upipe_h264f_stream_se(s); /* offset_for_top_to_bottom_field */
        uint32_t cycle = upipe_h264f_stream_ue(s);
        if (cycle > 256) {
            upipe_err_va(upipe, "invalid num_ref_frames_in_poc_cycle %"PRIu32,
                         cycle);
            ubuf_block_stream_clean(s);
            uref_free(flow_def);
            return false;
        }
        while (cycle > 0) {
            upipe_h264f_stream_se(s); /* offset_for_ref_frame[i] */
            cycle--;
        }
    }

    upipe_h264f_stream_ue(s); /* max_num_ref_frames */
    upipe_h264f_stream_fill_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1); /* gaps_in_frame_num_value_allowed */

    uint64_t mb_width = upipe_h264f_stream_ue(s) + 1;
    ret = ret && uref_pic_set_hsize(flow_def, mb_width * 16);

    uint64_t map_height = upipe_h264f_stream_ue(s) + 1;
    upipe_h264f_stream_fill_bits(s, 4);
    upipe_h264f->frame_mbs_only = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (!upipe_h264f->frame_mbs_only) {
        ret = ret && uref_pic_set_vsize(flow_def, map_height * 16 * 2);
        ubuf_block_stream_skip_bits(s, 1); /* mb_adaptive_frame_field */
    } else
        ret = ret && uref_pic_set_vsize(flow_def, map_height * 16);
    ubuf_block_stream_skip_bits(s, 1); /* direct8x8_inference */

    bool frame_cropping = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1); /* direct8x8_inference */
    if (frame_cropping) {
        upipe_h264f_stream_ue(s); /* left */
        upipe_h264f_stream_ue(s); /* right */
        upipe_h264f_stream_ue(s); /* top */
        upipe_h264f_stream_ue(s); /* bottom */
    }

    upipe_h264f_stream_fill_bits(s, 1);
    bool vui = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    upipe_h264f->sar.den = 0;
    if (vui) {
        upipe_h264f_stream_fill_bits(s, 1);
        bool ar_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (ar_present) {
            upipe_h264f_stream_fill_bits(s, 8);
            uint8_t ar_idc = ubuf_block_stream_show_bits(s, 8);
            ubuf_block_stream_skip_bits(s, 8);
            if (ar_idc > 0 &&
                ar_idc < sizeof(sar_from_idc) / sizeof(struct urational)) {
                upipe_h264f->sar = sar_from_idc[ar_idc];
                ret = ret && uref_pic_set_aspect(flow_def, upipe_h264f->sar);
            } else if (ar_idc == H264VUI_AR_EXTENDED) {
                upipe_h264f_stream_fill_bits(s, 16);
                upipe_h264f->sar.num = ubuf_block_stream_show_bits(s, 16);
                ubuf_block_stream_skip_bits(s, 16);
                upipe_h264f_stream_fill_bits(s, 16);
                upipe_h264f->sar.den = ubuf_block_stream_show_bits(s, 16);
                ubuf_block_stream_skip_bits(s, 16);
                ret = ret && uref_pic_set_aspect(flow_def, upipe_h264f->sar);
            } else
                upipe_warn_va(upipe, "unknown aspect ratio idc %"PRIu8, ar_idc);
        }

        upipe_h264f_stream_fill_bits(s, 3);
        bool overscan_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (overscan_present) {
            if (ubuf_block_stream_show_bits(s, 1))
                ret = ret && uref_pic_set_overscan(flow_def);
            ubuf_block_stream_skip_bits(s, 1);
        }

        bool video_signal_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (video_signal_present) {
            upipe_h264f_stream_fill_bits(s, 5);
            ubuf_block_stream_skip_bits(s, 4);
            if (ubuf_block_stream_show_bits(s, 1)) {
                upipe_h264f_stream_fill_bits(s, 24);
                ubuf_block_stream_skip_bits(s, 24);
            }
            upipe_h264f_stream_fill_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }

        upipe_h264f_stream_fill_bits(s, 1);
        bool chroma_loc_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (chroma_loc_present) {
            upipe_h264f_stream_ue(s);
            upipe_h264f_stream_ue(s);
        }

        upipe_h264f_stream_fill_bits(s, 1);
        bool timing_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (timing_present) {
            upipe_h264f_stream_fill_bits(s, 24);
            uint32_t num_units_in_ticks =
                ubuf_block_stream_show_bits(s, 24) << 8;
            ubuf_block_stream_skip_bits(s, 24);

            upipe_h264f_stream_fill_bits(s, 24);
            num_units_in_ticks |= ubuf_block_stream_show_bits(s, 8);
            ubuf_block_stream_skip_bits(s, 8);
            upipe_h264f->time_scale = ubuf_block_stream_show_bits(s, 16) << 16;
            ubuf_block_stream_skip_bits(s, 16);

            upipe_h264f_stream_fill_bits(s, 17);
            upipe_h264f->time_scale |= ubuf_block_stream_show_bits(s, 16);
            ubuf_block_stream_skip_bits(s, 16);
            if (ubuf_block_stream_show_bits(s, 1)) { /* fixed_frame_rate */
                struct urational frame_rate = {
                    .num = upipe_h264f->time_scale,
                    .den = num_units_in_ticks
                };
                urational_simplify(&frame_rate);
                ret = ret && uref_pic_flow_set_fps(flow_def, frame_rate);
                if (frame_rate.num)
                    upipe_h264f->duration = UCLOCK_FREQ * frame_rate.den /
                                            frame_rate.num;
            }
            ubuf_block_stream_skip_bits(s, 1);
        }

        uint64_t octetrate, cpb_size;
        upipe_h264f_stream_fill_bits(s, 1);
        bool nal_hrd_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (nal_hrd_present &&
            upipe_h264f_stream_parse_hrd(upipe, s, &octetrate, &cpb_size)) {
            ret = ret && uref_block_flow_set_octetrate(flow_def, octetrate);
            ret = ret && uref_block_flow_set_cpb_buffer(flow_def, cpb_size);
            upipe_h264f->octet_rate = octetrate;
            upipe_h264f->cpb_length = cpb_size * UCLOCK_FREQ / octetrate;
        }

        upipe_h264f_stream_fill_bits(s, 1);
        bool vcl_hrd_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (vcl_hrd_present &&
            upipe_h264f_stream_parse_hrd(upipe, s, &octetrate, &cpb_size)) {
            ret = ret && uref_block_flow_set_octetrate(flow_def, octetrate);
            ret = ret && uref_block_flow_set_cpb_buffer(flow_def, cpb_size);
            upipe_h264f->cpb_length = cpb_size * UCLOCK_FREQ / octetrate;
        }

        if (nal_hrd_present || vcl_hrd_present) {
            upipe_h264f_stream_fill_bits(s, 1);
            if (!!ubuf_block_stream_show_bits(s, 1))
                ret = ret && uref_flow_set_lowdelay(flow_def);
            ubuf_block_stream_skip_bits(s, 1);
            upipe_h264f->hrd = true;
        } else
            upipe_h264f->hrd = false;

        upipe_h264f_stream_fill_bits(s, 1);
        upipe_h264f->pic_struct_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
    } else {
        upipe_h264f->duration = 0;
        upipe_h264f->pic_struct_present = false;
        upipe_h264f->hrd = false;
    }

    upipe_h264f->active_sps = sps_id;
    ubuf_block_stream_clean(s);
    upipe_h264f_store_flow_def(upipe, flow_def);
    return true;
}

/** @internal @This activates a picture parameter set.
 *
 * @param upipe description structure of the pipe
 * @param pps_id PPS to activate
 * @return false if the PPS couldn't be activated
 */
static bool upipe_h264f_activate_pps(struct upipe *upipe, uint32_t pps_id)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (likely(upipe_h264f->active_pps == pps_id))
        return true;
    if (unlikely(upipe_h264f->pps[pps_id] == NULL))
        return false;

    struct upipe_h264f_stream f;
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, upipe_h264f->pps[pps_id], 5);
    assert(ret);

    upipe_h264f_stream_ue(s); /* pps_id */
    uint32_t sps_id = upipe_h264f_stream_ue(s);
    if (!upipe_h264f_activate_sps(upipe, sps_id)) {
        ubuf_block_stream_clean(s);
        return false;
    }

    upipe_h264f_stream_fill_bits(s, 2);
    ubuf_block_stream_skip_bits(s, 1);
    upipe_h264f->bf_poc = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);

    upipe_h264f->active_pps = pps_id;
    ubuf_block_stream_clean(s);
    return true;
}

/** @internal @This handles the supplemental enhancement information called
 * buffering period.
 *
 * @param upipe description structure of the pipe
 * @param s block stream parsing structure
 */
static void upipe_h264f_handle_sei_buffering_period(struct upipe *upipe,
                                                    struct ubuf_block_stream *s)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint32_t sps_id = upipe_h264f_stream_ue(s);
    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32" in SEI", sps_id);
        ubuf_block_stream_clean(s);
        return;
    }

    if (!upipe_h264f_activate_sps(upipe, sps_id)) {
        ubuf_block_stream_clean(s);
        return;
    }

    if (upipe_h264f->hrd) {
        size_t initial_cpb_removal_delay_length =
            upipe_h264f->initial_cpb_removal_delay_length;
        upipe_h264f->initial_cpb_removal_delay = 0;
        if (initial_cpb_removal_delay_length > 24) {
            upipe_h264f_stream_fill_bits(s, 24);
            upipe_h264f->initial_cpb_removal_delay =
                ubuf_block_stream_show_bits(s, 24);
            ubuf_block_stream_skip_bits(s, 24);
            initial_cpb_removal_delay_length -= 24;
            upipe_h264f->initial_cpb_removal_delay <<=
                initial_cpb_removal_delay_length;
        }
        upipe_h264f_stream_fill_bits(s, initial_cpb_removal_delay_length);
        upipe_h264f->initial_cpb_removal_delay |=
            ubuf_block_stream_show_bits(s, initial_cpb_removal_delay_length);
        ubuf_block_stream_skip_bits(s, initial_cpb_removal_delay_length);
        upipe_h264f->initial_cpb_removal_delay *= UCLOCK_FREQ / 90000;
    }
}

/** @internal @This handles the supplemental enhancement information called
 * picture timing.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sei_pic_timing(struct upipe *upipe,
                                              struct ubuf_block_stream *s)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->hrd) {
        size_t cpb_removal_delay_length =
            upipe_h264f->cpb_removal_delay_length;
        if (cpb_removal_delay_length > 24) {
            upipe_h264f_stream_fill_bits(s, 24);
            ubuf_block_stream_skip_bits(s, 24);
            cpb_removal_delay_length -= 24;
        }
        upipe_h264f_stream_fill_bits(s, cpb_removal_delay_length);
        ubuf_block_stream_skip_bits(s, cpb_removal_delay_length);

        size_t dpb_output_delay_length =
            upipe_h264f->dpb_output_delay_length;
        if (dpb_output_delay_length > 24) {
            upipe_h264f_stream_fill_bits(s, 24);
            ubuf_block_stream_skip_bits(s, 24);
            dpb_output_delay_length -= 24;
        }
        upipe_h264f_stream_fill_bits(s, dpb_output_delay_length);
        ubuf_block_stream_skip_bits(s, dpb_output_delay_length);
    }

    if (upipe_h264f->pic_struct_present) {
        upipe_h264f_stream_fill_bits(s, 4);
        upipe_h264f->pic_struct = ubuf_block_stream_show_bits(s, 4);
        ubuf_block_stream_skip_bits(s, 4);
    }
}

/** @internal @This handles a supplemental enhancement information.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_sei(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint8_t type;
    if (unlikely(!uref_block_extract(upipe_h264f->next_uref,
                                     upipe_h264f->au_last_nal_offset +
                                     upipe_h264f->au_last_nal_start_size, 1,
                                     &type)))
        return;
    if (type != H264SEI_BUFFERING_PERIOD && type != H264SEI_PIC_TIMING)
        return;

    struct upipe_h264f_stream f;
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, upipe_h264f->next_uref->ubuf,
                                      upipe_h264f->au_last_nal_offset +
                                      upipe_h264f->au_last_nal_start_size + 1);
    assert(ret);

    /* size field */
    uint8_t octet;
    do {
        upipe_h264f_stream_fill_bits(s, 8);
        octet = ubuf_block_stream_show_bits(s, 8);
        ubuf_block_stream_skip_bits(s, 8);
    } while (octet == UINT8_MAX);

    switch (type) {
        case H264SEI_BUFFERING_PERIOD:
            upipe_h264f_handle_sei_buffering_period(upipe, s);
            break;
        case H264SEI_PIC_TIMING:
            upipe_h264f_handle_sei_pic_timing(upipe, s);
            break;
        default:
            break;
    }

    ubuf_block_stream_clean(s);
}

/** @internal @This handles and outputs an access unit.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_output_au(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (!upipe_h264f->au_size || upipe_h264f->au_slice_nal == UINT8_MAX)
        return;

#define KEEP_TIMESTAMP(name)                                                \
    uint64_t name = upipe_h264f->au_##name;
    KEEP_TIMESTAMP(pts_orig)
    KEEP_TIMESTAMP(pts)
    KEEP_TIMESTAMP(pts_sys)
    KEEP_TIMESTAMP(dts_orig)
    KEEP_TIMESTAMP(dts)
    KEEP_TIMESTAMP(dts_sys)
#undef KEEP_TIMESTAMP
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_h264f_flush_pts(upipe);
    upipe_h264f_flush_dts(upipe);

    struct uref *uref = upipe_h264f_extract_uref_stream(upipe,
                                                        upipe_h264f->au_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    bool ret = true;
    uint64_t picture_number = upipe_h264f->last_picture_number +
        (upipe_h264f->frame_num - upipe_h264f->last_frame_num);
    if (upipe_h264f->frame_num > upipe_h264f->last_frame_num) {
        upipe_h264f->last_frame_num = upipe_h264f->frame_num;
        upipe_h264f->last_picture_number = picture_number;
    }
    ret = ret && uref_pic_set_number(uref, picture_number);

    uint64_t duration = upipe_h264f->duration;
    if (upipe_h264f->pic_struct == -1) {
        if (upipe_h264f->field_pic)
            upipe_h264f->pic_struct = upipe_h264f->bf ? H264SEI_STRUCT_BOT :
                                                        H264SEI_STRUCT_TOP;
        else {
            int32_t delta_poc_bottom = 0;
            if (upipe_h264f->poc_type == 0)
                delta_poc_bottom = upipe_h264f->delta_poc_bottom;
            else if (upipe_h264f->poc_type == 1 &&
                     !upipe_h264f->delta_poc_always_zero)
                delta_poc_bottom = upipe_h264f->delta_poc1 -
                                   upipe_h264f->delta_poc0;
            if (delta_poc_bottom == 0)
                upipe_h264f->pic_struct = H264SEI_STRUCT_FRAME;
            else if (delta_poc_bottom < 0)
                upipe_h264f->pic_struct = H264SEI_STRUCT_TOP_BOT;
            else
                upipe_h264f->pic_struct = H264SEI_STRUCT_BOT_TOP;
        }
    }

    switch (upipe_h264f->pic_struct) {
        case H264SEI_STRUCT_FRAME:
            ret = ret && uref_pic_set_progressive(uref);
            duration *= 2;
            break;
        case H264SEI_STRUCT_TOP:
            ret = ret && uref_pic_set_tf(uref);
            break;
        case H264SEI_STRUCT_BOT:
            ret = ret && uref_pic_set_bf(uref);
            break;
        case H264SEI_STRUCT_TOP_BOT:
            ret = ret && uref_pic_set_tf(uref);
            ret = ret && uref_pic_set_bf(uref);
            ret = ret && uref_pic_set_tff(uref);
            duration *= 2;
            break;
        case H264SEI_STRUCT_BOT_TOP:
            ret = ret && uref_pic_set_tf(uref);
            ret = ret && uref_pic_set_bf(uref);
            duration *= 2;
            break;
        case H264SEI_STRUCT_TOP_BOT_TOP:
            ret = ret && uref_pic_set_tf(uref);
            ret = ret && uref_pic_set_bf(uref);
            ret = ret && uref_pic_set_tff(uref);
            duration *= 3;
            break;
        case H264SEI_STRUCT_BOT_TOP_BOT:
            ret = ret && uref_pic_set_tf(uref);
            ret = ret && uref_pic_set_bf(uref);
            duration *= 3;
            break;
        case H264SEI_STRUCT_DOUBLE:
            duration *= 4;
            break;
        case H264SEI_STRUCT_TRIPLE:
            duration *= 6;
            break;
        default:
            upipe_warn_va(upipe, "invalid picture structure %"PRId32,
                          upipe_h264f->pic_struct);
            break;
    }
    if (duration)
        ret = ret && uref_clock_set_duration(uref, duration);

#define SET_TIMESTAMP(name)                                                 \
    if (name != UINT64_MAX)                                                 \
        ret = ret && uref_clock_set_##name(uref, name);                     \
    else                                                                    \
        uref_clock_delete_##name(uref);
    SET_TIMESTAMP(pts_orig)
    SET_TIMESTAMP(pts)
    SET_TIMESTAMP(pts_sys)
    SET_TIMESTAMP(dts_orig)
    SET_TIMESTAMP(dts)
    SET_TIMESTAMP(dts_sys)
#undef SET_TIMESTAMP

#define INCREMENT_DTS(name)                                                 \
    if (upipe_h264f->au_##name == UINT64_MAX && name != UINT64_MAX)         \
        upipe_h264f->au_##name = name + duration;
    INCREMENT_DTS(dts_orig)
    INCREMENT_DTS(dts)
    INCREMENT_DTS(dts_sys)
#undef INCREMENT_DTS

    if (upipe_h264f->systime_rap != UINT64_MAX)
        ret = ret && uref_clock_set_systime_rap(uref, upipe_h264f->systime_rap);
    if (upipe_h264f->au_vcl_offset > 0)
        ret = ret && uref_block_set_header_size(uref,
                                                upipe_h264f->au_vcl_offset);

    if (upipe_h264f->initial_cpb_removal_delay != INT64_MAX &&
        upipe_h264f->octet_rate > 0) {
        upipe_h264f->initial_cpb_removal_delay += upipe_h264f->duration;
        upipe_h264f->initial_cpb_removal_delay -=
            upipe_h264f->au_size * UCLOCK_FREQ / upipe_h264f->octet_rate;

        if (upipe_h264f->initial_cpb_removal_delay < 0) {
            upipe_warn_va(upipe, "CPB underflow "PRId64,
                          -upipe_h264f->initial_cpb_removal_delay);
            upipe_h264f->initial_cpb_removal_delay = 0;
        } else if (upipe_h264f->initial_cpb_removal_delay >
                        upipe_h264f->cpb_length)
            upipe_h264f->initial_cpb_removal_delay = upipe_h264f->cpb_length;

        ret = ret && uref_clock_set_vbv_delay(uref,
                            upipe_h264f->initial_cpb_removal_delay);
    }

    upipe_h264f->au_size = 0;
    upipe_h264f->au_vcl_offset = -1;
    upipe_h264f->au_slice = false;
    upipe_h264f->au_slice_nal = UINT8_MAX;
    upipe_h264f->pic_struct = -1;

    if (unlikely(!ret)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    upipe_h264f_output(upipe, uref, upump);
}

/** @internal @This outputs the previous access unit, before the current NAL.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_output_prev_au(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    size_t slice_size = upipe_h264f->au_size -
                        upipe_h264f->au_last_nal_offset;
    upipe_h264f->au_size = upipe_h264f->au_last_nal_offset;
    upipe_h264f_output_au(upipe, upump);
    upipe_h264f->au_size = slice_size;
    upipe_h264f->au_last_nal_offset = 0;
}

/** @internal @This parses a slice header, and optionally outputs previous
 * access unit if it is the start of a new one.
 *
 * @param upipe description structure of the pipe
 * @param upump pump that generated the buffer
 */
static void upipe_h264f_parse_slice(struct upipe *upipe, struct upump *upump)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    struct upipe_h264f_stream f;
upipe_h264f_parse_slice_retry:
    upipe_h264f_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    bool ret = ubuf_block_stream_init(s, upipe_h264f->next_uref->ubuf,
                                      upipe_h264f->au_last_nal_offset +
                                      upipe_h264f->au_last_nal_start_size);
    assert(ret);

    upipe_h264f_stream_ue(s); /* first_mb_in_slice */
    upipe_h264f_stream_ue(s); /* slice_type */
    uint32_t pps_id = upipe_h264f_stream_ue(s);
    if (unlikely(pps_id >= H264PPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid PPS %"PRIu32" in slice", pps_id);
        ubuf_block_stream_clean(s);
        return;
    }

    if (upipe_h264f->au_slice && pps_id != upipe_h264f->active_pps) {
        ubuf_block_stream_clean(s);
        upipe_h264f_output_prev_au(upipe, upump);
        goto upipe_h264f_parse_slice_retry;
    }

    if (unlikely(!upipe_h264f_activate_pps(upipe, pps_id))) {
        ubuf_block_stream_clean(s);
        return;
    }

    if (upipe_h264f->separate_colour_plane) {
        upipe_h264f_stream_fill_bits(s, 2);
        ubuf_block_stream_skip_bits(s, 2);
    }
    uint32_t frame_num = ubuf_block_stream_show_bits(s,
            upipe_h264f->log2_max_frame_num);
    ubuf_block_stream_skip_bits(s, upipe_h264f->log2_max_frame_num);
    bool field_pic = false;
    bool bf = false;
    if (!upipe_h264f->frame_mbs_only) {
        upipe_h264f_stream_fill_bits(s, 2);
        field_pic = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (field_pic) {
            bf = !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }
    }

    uint32_t idr_pic_id = upipe_h264f->idr_pic_id;
    if (h264nalst_get_type(upipe_h264f->au_slice_nal) == H264NAL_TYPE_IDR)
        idr_pic_id = upipe_h264f_stream_ue(s);

    if (upipe_h264f->au_slice &&
        (frame_num != upipe_h264f->frame_num ||
         field_pic != upipe_h264f->field_pic ||
         bf != upipe_h264f->bf ||
         idr_pic_id != upipe_h264f->idr_pic_id)) {
        ubuf_block_stream_clean(s);
        upipe_h264f_output_prev_au(upipe, upump);
        goto upipe_h264f_parse_slice_retry;
    }
    upipe_h264f->frame_num = frame_num;
    upipe_h264f->field_pic = field_pic;
    upipe_h264f->bf = bf;
    upipe_h264f->idr_pic_id = idr_pic_id;

    if (upipe_h264f->poc_type == 0) {
        upipe_h264f_stream_fill_bits(s, upipe_h264f->log2_max_poc_lsb);
        uint32_t poc_lsb = ubuf_block_stream_show_bits(s,
                upipe_h264f->log2_max_poc_lsb);
        ubuf_block_stream_skip_bits(s, upipe_h264f->log2_max_poc_lsb);
        int32_t delta_poc_bottom = 0;
        if (upipe_h264f->bf_poc && !field_pic)
            delta_poc_bottom = upipe_h264f_stream_se(s);

        if (upipe_h264f->au_slice &&
            (poc_lsb != upipe_h264f->poc_lsb ||
             delta_poc_bottom != upipe_h264f->delta_poc_bottom)) {
            ubuf_block_stream_clean(s);
            upipe_h264f_output_prev_au(upipe, upump);
            goto upipe_h264f_parse_slice_retry;
        }
        upipe_h264f->poc_lsb = poc_lsb;
        upipe_h264f->delta_poc_bottom = delta_poc_bottom;

    } else if (upipe_h264f->poc_type == 1 &&
               !upipe_h264f->delta_poc_always_zero) {
        int32_t delta_poc0 = upipe_h264f_stream_se(s);
        int32_t delta_poc1 = 0;
        if (upipe_h264f->bf_poc && !field_pic)
            delta_poc1 = upipe_h264f_stream_se(s);

        if (upipe_h264f->au_slice &&
            (delta_poc0 != upipe_h264f->delta_poc0 ||
             delta_poc1 != upipe_h264f->delta_poc1)) {
            ubuf_block_stream_clean(s);
            upipe_h264f_output_prev_au(upipe, upump);
            goto upipe_h264f_parse_slice_retry;
        }
        upipe_h264f->delta_poc0 = delta_poc0;
        upipe_h264f->delta_poc1 = delta_poc1;
    }
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
    if (last_nal_type == H264NAL_TYPE_NONIDR ||
        last_nal_type == H264NAL_TYPE_PARTA ||
        last_nal_type == H264NAL_TYPE_PARTB ||
        last_nal_type == H264NAL_TYPE_PARTC ||
        last_nal_type == H264NAL_TYPE_IDR) {
        if (unlikely(upipe_h264f->got_discontinuity))
            uref_flow_set_error(upipe_h264f->next_uref);
        else
            upipe_h264f_parse_slice(upipe, upump);
        if (last_nal_type == H264NAL_TYPE_IDR) {
            uint64_t systime_rap;
            if (uref_clock_get_systime_rap(upipe_h264f->next_uref,
                                           &systime_rap) && systime_rap)
                upipe_h264f->systime_rap = systime_rap;
        }
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
        case H264NAL_TYPE_SEI:
            upipe_h264f_handle_sei(upipe);
            break;
        case H264NAL_TYPE_SPS:
            upipe_h264f_handle_sps(upipe);
            break;
        case H264NAL_TYPE_SPSX:
            upipe_h264f_handle_sps_ext(upipe);
            break;
        case H264NAL_TYPE_PPS:
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
        case H264NAL_TYPE_NONIDR:
        case H264NAL_TYPE_PARTA:
        case H264NAL_TYPE_PARTB:
        case H264NAL_TYPE_PARTC:
        case H264NAL_TYPE_IDR: {
            uint8_t slice_nal = upipe_h264f->au_slice_nal;
            upipe_h264f->au_slice_nal = upipe_h264f->au_last_nal;

            if (upipe_h264f->au_vcl_offset == -1) {
                upipe_h264f->au_vcl_offset =
                    upipe_h264f->au_size - upipe_h264f->au_last_nal_start_size;
                return false;
            }
            if (slice_nal == UINT8_MAX)
                return false;
            if ((h264nalst_get_type(slice_nal) == H264NAL_TYPE_IDR) ==
                    (nal_type == H264NAL_TYPE_IDR) &&
                (h264nalst_get_ref(upipe_h264f->au_last_nal) == 0) ==
                    (h264nalst_get_ref(slice_nal) == 0))
                return false;
            /* new access unit */
            break;
        }

        case H264NAL_TYPE_SEI:
            if (upipe_h264f->au_vcl_offset == -1) {
                upipe_h264f->au_vcl_offset =
                    upipe_h264f->au_size - upipe_h264f->au_last_nal_start_size;
                return false;
            }
            if (upipe_h264f->au_slice_nal == UINT8_MAX)
                return false;
            break;

        case H264NAL_TYPE_ENDSEQ:
        case H264NAL_TYPE_ENDSTR:
            /* immediately output everything and jump out */
            upipe_h264f_output_au(upipe, upump);
            return true;

        case H264NAL_TYPE_AUD:
        case H264NAL_TYPE_SPS:
        case H264NAL_TYPE_SPSX:
        case H264NAL_TYPE_SSPS:
        case H264NAL_TYPE_PPS:
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
    if (unlikely(uref->ubuf == NULL)) {
        upipe_h264f_output(upipe, uref, upump);
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
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_h264f_get_flow_def(upipe, p);
        }
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
    }

    for (i = 0; i < H264PPS_ID_MAX; i++)
        if (upipe_h264f->pps[i] != NULL)
            ubuf_free(upipe_h264f->pps[i]);

    upipe_h264f_free_flow(upipe);
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

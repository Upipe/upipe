/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of an ISO 14496-10 stream
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_sync.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_flow_format.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/uref_h264.h>
#include <upipe-framers/uref_h26x.h>
#include <upipe-framers/uref_h264_flow.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-framers/upipe_framers_common.h>
#include <upipe-framers/upipe_h26x_common.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/h264.h>

/** @internal @This is the private context of an h264f pipe. */
struct upipe_h264f {
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
    /** attributes added by the pipe */
    struct uref *flow_def_attr;
    /** requested flow definition */
    struct uref *flow_def_requested;
    /** input H26x encapsulation */
    enum uref_h26x_encaps encaps_input;
    /** output H26x encapsulation */
    enum uref_h26x_encaps encaps_output;
    /** complete input */
    bool complete_input;

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
    /** buffered output uref (used during urequest) */
    struct uref *uref_output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** ubuf containing an annex B header */
    struct ubuf *annexb_header;
    /** ubuf containing an annex B access unit delimiter */
    struct ubuf *annexb_aud;

    /** rap of the last dts */
    uint64_t dts_rap;
    /** rap of the last sps */
    uint64_t sps_rap;
    /** rap of the last pps */
    uint64_t pps_rap;
    /** rap of the last I */
    uint64_t iframe_rap;
    /** latency in the input flow */
    uint64_t input_latency;

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
    /** profile */
    uint8_t profile;
    /** profile compatibility */
    uint8_t profile_compatibility;
    /** level */
    uint8_t level;
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
    /** duration of a field */
    uint64_t duration;
    /** true if picture structure is present */
    bool pic_struct_present;
    /** true if bottom_field_pic_order_in_frame is present */
    bool bf_poc;
    /** maximum frame buffering */
    uint32_t max_dec_frame_buffering;

    /* parsing results - slice */
    /** picture structure */
    int pic_struct;
    /** DPB output delay */
    uint64_t dpb_output_delay;
    /** frame number */
    uint32_t frame_num;
    /** slice type */
    uint32_t slice_type;
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
    struct uchain urefs;

    /* octet stream parser stuff */
    /** context of the scan function */
    uint32_t scan_context;
    /** current size of next access unit (in next_uref) */
    size_t au_size;
    /** number of NAL units in next_uref minus 1 */
    uint64_t au_nal_units;
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
    /** pseudo-packet containing date information for the next picture */
    struct uref au_uref_s;
    /** drift rate of the next picture */
    struct urational drift_rate;
    /** true if we have thrown the sync_acquired event (that means we found a
     * NAL start) */
    bool acquired;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static void upipe_h264f_promote_uref(struct upipe *upipe);
/** @hidden */
static bool upipe_h264f_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p);
/** @hidden */
static int upipe_h264f_check_flow_format(struct upipe *upipe,
                                         struct uref *flow_format);
/** @hidden */
static int upipe_h264f_check_ubuf_mgr(struct upipe *upipe,
                                      struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_h264f, upipe, UPIPE_H264F_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_h264f, urefcount, upipe_h264f_free)
UPIPE_HELPER_VOID(upipe_h264f)
UPIPE_HELPER_SYNC(upipe_h264f, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_h264f, next_uref, next_uref_size, urefs,
                         upipe_h264f_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_h264f, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_h264f, request_urefs, nb_urefs, max_urefs, blockers, upipe_h264f_handle)
UPIPE_HELPER_FLOW_FORMAT(upipe_h264f, request, upipe_h264f_check_flow_format,
                         upipe_h264f_register_output_request,
                         upipe_h264f_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_h264f, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_h264f, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_h264f_check_ubuf_mgr,
                      upipe_h264f_register_output_request,
                      upipe_h264f_unregister_output_request)

/** @internal @This flushes all dates.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_flush_dates(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uref_clock_set_date_sys(&upipe_h264f->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_h264f->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_h264f->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_delete_dts_pts_delay(&upipe_h264f->au_uref_s);

    upipe_h264f->drift_rate.num = upipe_h264f->drift_rate.den = 0;
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
    struct upipe *upipe = upipe_h264f_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_h264f_init_urefcount(upipe);
    upipe_h264f_init_sync(upipe);
    upipe_h264f_init_uref_stream(upipe);
    upipe_h264f_init_output(upipe);
    upipe_h264f_init_input(upipe);
    upipe_h264f_init_flow_format(upipe);
    upipe_h264f_init_flow_def(upipe);
    upipe_h264f_init_ubuf_mgr(upipe);
    upipe_h264f->flow_def_requested = NULL;
    upipe_h264f->encaps_input = upipe_h264f->encaps_output =
        UREF_H26X_ENCAPS_ANNEXB;
    upipe_h264f->complete_input = false;
    upipe_h264f->uref_output = NULL;
    upipe_h264f->annexb_header = NULL;
    upipe_h264f->annexb_aud = NULL;
    upipe_h264f->dts_rap = UINT64_MAX;
    upipe_h264f->sps_rap = UINT64_MAX;
    upipe_h264f->pps_rap = UINT64_MAX;
    upipe_h264f->iframe_rap = UINT64_MAX;
    upipe_h264f->input_latency = 0;
    upipe_h264f->last_picture_number = 0;
    upipe_h264f->last_frame_num = -1;
    upipe_h264f->max_dec_frame_buffering = UINT32_MAX;
    upipe_h264f->pic_struct = -1;
    upipe_h264f->dpb_output_delay = UINT64_MAX;
    upipe_h264f->duration = 0;
    upipe_h264f->got_discontinuity = false;
    upipe_h264f->scan_context = UINT32_MAX;
    upipe_h264f->au_size = 0;
    upipe_h264f->au_last_nal_offset = -1;
    upipe_h264f->au_last_nal = UINT8_MAX;
    upipe_h264f->au_last_nal_start_size = 0;
    upipe_h264f->au_nal_units = 0;
    upipe_h264f->au_vcl_offset = -1;
    upipe_h264f->au_slice = false;
    upipe_h264f->au_slice_nal = UINT8_MAX;
    uref_init(&upipe_h264f->au_uref_s);
    upipe_h264f_flush_dates(upipe);

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

/** @internal @This parses scaling matrices.
 *
 * @param s ubuf block stream
 */
static void upipe_h264f_stream_parse_scaling(struct ubuf_block_stream *s,
                                             int nb_lists)
{
    int i, j;
    for (i = 0; i < nb_lists; i++) {
        upipe_h26xf_stream_fill_bits(s, 1);
        bool seq_scaling_list = ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (!seq_scaling_list)
            continue;

        int scaling_list_size = (i < 6) ? 16 : 64;
        int32_t last_scale = 8, next_scale = 8;
        for (j = 0; j < scaling_list_size; j++) {
            if (next_scale != 0)
                next_scale = (last_scale + upipe_h26xf_stream_se(s) + 256) %
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
 * @param cpb_size_p filled in with the CPB buffer size
 * @return UBASE_ERR_NONE if the hrd parameters were parsed successfully
 */
static int upipe_h264f_stream_parse_hrd(struct upipe *upipe,
                                        struct ubuf_block_stream *s,
                                        uint64_t *octetrate_p,
                                        uint64_t *cpb_size_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint32_t cpb_cnt = upipe_h26xf_stream_ue(s) + 1;
    if (!cpb_cnt || cpb_cnt > 32) {
        upipe_warn_va(upipe, "invalid cpb_cnt %"PRIu32, cpb_cnt);
        return UBASE_ERR_INVALID;
    }
    upipe_h26xf_stream_fill_bits(s, 8);
    uint8_t bitrate_scale = ubuf_block_stream_show_bits(s, 4);
    ubuf_block_stream_skip_bits(s, 4);
    uint8_t cpb_size_scale = ubuf_block_stream_show_bits(s, 4);
    ubuf_block_stream_skip_bits(s, 4);

    /* Use first value to deduce bitrate and cpb size */
    *octetrate_p =
        (((uint64_t)upipe_h26xf_stream_ue(s) + 1) << (6 + bitrate_scale)) / 8;
    *cpb_size_p =
        (((uint64_t)upipe_h26xf_stream_ue(s) + 1) << (4 + cpb_size_scale)) / 8;
    upipe_h26xf_stream_fill_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1); /* cbr_flag */
    cpb_cnt--;

    /* Next values dropped, if present */
    while (cpb_cnt) {
        upipe_h26xf_stream_ue(s);
        upipe_h26xf_stream_ue(s);
        upipe_h26xf_stream_fill_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        cpb_cnt--;
    }

    upipe_h26xf_stream_fill_bits(s, 20);
    upipe_h264f->initial_cpb_removal_delay_length =
        ubuf_block_stream_show_bits(s, 5) + 1;
    ubuf_block_stream_skip_bits(s, 5);
    upipe_h264f->cpb_removal_delay_length =
        ubuf_block_stream_show_bits(s, 5) + 1;
    ubuf_block_stream_skip_bits(s, 5);
    upipe_h264f->dpb_output_delay_length =
        ubuf_block_stream_show_bits(s, 5) + 1;
    ubuf_block_stream_skip_bits(s, 10);

    return UBASE_ERR_NONE;
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

    struct uref *flow_def = upipe_h264f_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))
    UBASE_FATAL(upipe, uref_h26x_flow_set_encaps(flow_def,
                upipe_h264f->encaps_input))

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, upipe_h264f->sps[sps_id], 1))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    upipe_h26xf_stream_fill_bits(s, 24);
    uint8_t profile = upipe_h264f->profile = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    UBASE_FATAL(upipe, uref_h264_flow_set_profile(flow_def, profile))

    uint8_t profile_compatibility = upipe_h264f->profile_compatibility =
        ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    UBASE_FATAL(upipe, uref_h264_flow_set_profile_compatibility(flow_def,
                profile_compatibility))

    uint8_t level = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    UBASE_FATAL(upipe, uref_h264_flow_set_level(flow_def, level))

    uint64_t max_octetrate, max_bs;
    switch (level) {
        case 10:
            max_octetrate = 64000 / 8;
            max_bs = 175000 / 8;
            break;
        case 11:
            max_octetrate = 192000 / 8;
            max_bs = 500000 / 8;
            break;
        case 12:
            max_octetrate = 384000 / 8;
            max_bs = 1000000 / 8;
            break;
        case 13:
            max_octetrate = 768000 / 8;
            max_bs = 2000000 / 8;
            break;
        case 20:
            max_octetrate = 2000000 / 8;
            max_bs = 2000000 / 8;
            break;
        case 21:
        case 22:
            max_octetrate = 4000000 / 8;
            max_bs = 4000000 / 8;
            break;
        case 30:
            max_octetrate = 10000000 / 8;
            max_bs = 10000000 / 8;
            break;
        case 31:
            max_octetrate = 14000000 / 8;
            max_bs = 14000000 / 8;
            break;
        case 32:
        case 40:
            max_octetrate = 20000000 / 8;
            max_bs = 20000000 / 8;
            break;
        case 41:
        case 42:
            max_octetrate = 50000000 / 8;
            max_bs = 62500000 / 8;
            break;
        case 50:
            max_octetrate = 135000000 / 8;
            max_bs = 135000000 / 8;
            break;
        default:
            upipe_warn_va(upipe, "unknown level %"PRIu8, level);
            /* fallthrough */
        case 51:
        case 52:
            max_octetrate = 240000000 / 8;
            max_bs = 240000000 / 8;
            break;
    }
    UBASE_FATAL(upipe, uref_block_flow_set_max_octetrate(flow_def, max_octetrate))
    UBASE_FATAL(upipe, uref_block_flow_set_max_buffer_size(flow_def, max_bs))

    upipe_h26xf_stream_ue(s); /* sps_id */
    uint32_t chroma_idc = 1;
    uint8_t luma_depth = 8, chroma_depth = 8;
    upipe_h264f->separate_colour_plane = false;
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 ||
        profile ==  44 || profile ==  83 || profile ==  86 || profile == 118 ||
        profile == 128)
    {
        chroma_idc = upipe_h26xf_stream_ue(s);
        if (chroma_idc == H264SPS_CHROMA_444) {
            upipe_h26xf_stream_fill_bits(s, 1);
            upipe_h264f->separate_colour_plane =
                !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }
        luma_depth += upipe_h26xf_stream_ue(s);
        if (!upipe_h264f->separate_colour_plane)
            chroma_depth += upipe_h26xf_stream_ue(s);
        else
            chroma_depth = luma_depth;
        upipe_h26xf_stream_fill_bits(s, 2);
        ubuf_block_stream_skip_bits(s, 1); /* qpprime_y_zero_transform_etc. */
        bool seq_scaling_matrix = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);

        if (seq_scaling_matrix)
            upipe_h264f_stream_parse_scaling(s,
                    chroma_idc != H264SPS_CHROMA_444 ? 8 : 12);
    }

    UBASE_FATAL(upipe, uref_pic_flow_set_macropixel(flow_def, 1))
    UBASE_FATAL(upipe, uref_pic_flow_set_planes(flow_def, 0))
    if (luma_depth == 8) {
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
    } else {
        UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y16"))
        luma_depth = 16;
    }
    if (chroma_idc == H264SPS_CHROMA_MONO) {
        UBASE_FATAL(upipe, uref_flow_set_def_va(flow_def,
                UPIPE_H264F_EXPECTED_FLOW_DEF "pic.planar%"PRIu8"_mono.",
                luma_depth))
    } else {
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
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, hsub, vsub, 1, "u8"))
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, hsub, vsub, 1, "v8"))
            UBASE_FATAL(upipe, uref_flow_set_def_va(flow_def,
                    UPIPE_H264F_EXPECTED_FLOW_DEF "pic.planar%"PRIu8"_8_%s.",
                    luma_depth, chroma))
        } else {
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, hsub, vsub, 2, "u16"))
            UBASE_FATAL(upipe, uref_pic_flow_add_plane(flow_def, hsub, vsub, 2, "v16"))
            UBASE_FATAL(upipe, uref_flow_set_def_va(flow_def,
                    UPIPE_H264F_EXPECTED_FLOW_DEF "pic.planar%"PRIu8"_16_%s.",
                    luma_depth, chroma))
        }
    }

    /* Skip i_log2_max_frame_num */
    upipe_h264f->log2_max_frame_num = 4 + upipe_h26xf_stream_ue(s);
    if (upipe_h264f->log2_max_frame_num > 16) {
        upipe_err_va(upipe, "invalid log2_max_frame_num %"PRIu32,
                     upipe_h264f->log2_max_frame_num);
        upipe_h264f->log2_max_frame_num = 0;
        ubuf_block_stream_clean(s);
        uref_free(flow_def);
        return false;
    }

    upipe_h264f->poc_type = upipe_h26xf_stream_ue(s);
    if (!upipe_h264f->poc_type) {
        upipe_h264f->log2_max_poc_lsb = 4 + upipe_h26xf_stream_ue(s);
        if (upipe_h264f->log2_max_poc_lsb > 16) {
            upipe_err_va(upipe, "invalid log2_max_poc_lsb %"PRIu32,
                         upipe_h264f->log2_max_poc_lsb);
            upipe_h264f->log2_max_poc_lsb = 0;
            ubuf_block_stream_clean(s);
            uref_free(flow_def);
            return false;
        }

    } else if (upipe_h264f->poc_type == 1) {
        upipe_h26xf_stream_fill_bits(s, 1);
        upipe_h264f->delta_poc_always_zero =
            !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        upipe_h26xf_stream_se(s); /* offset_for_non_ref_pic */
        upipe_h26xf_stream_se(s); /* offset_for_top_to_bottom_field */
        uint32_t cycle = upipe_h26xf_stream_ue(s);
        if (cycle > 256) {
            upipe_err_va(upipe, "invalid num_ref_frames_in_poc_cycle %"PRIu32,
                         cycle);
            ubuf_block_stream_clean(s);
            uref_free(flow_def);
            return false;
        }
        while (cycle > 0) {
            upipe_h26xf_stream_se(s); /* offset_for_ref_frame[i] */
            cycle--;
        }
    }

    upipe_h26xf_stream_ue(s); /* max_num_ref_frames */
    upipe_h26xf_stream_fill_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1); /* gaps_in_frame_num_value_allowed */

    uint64_t mb_width = upipe_h26xf_stream_ue(s) + 1;
    uint64_t hsize = mb_width * 16;

    uint64_t map_height = upipe_h26xf_stream_ue(s) + 1;
    upipe_h26xf_stream_fill_bits(s, 4);
    upipe_h264f->frame_mbs_only = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    uint64_t vsize;
    if (!upipe_h264f->frame_mbs_only) {
        vsize = map_height * 16 * 2;
        ubuf_block_stream_skip_bits(s, 1); /* mb_adaptive_frame_field */
    } else {
        UBASE_FATAL(upipe, uref_pic_set_progressive(flow_def))
        vsize = map_height * 16;
    }
    ubuf_block_stream_skip_bits(s, 1); /* direct8x8_inference */

    bool frame_cropping = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1); /* direct8x8_inference */
    if (frame_cropping) {
        uint32_t crop_left = upipe_h26xf_stream_ue(s);
        uint32_t crop_right = upipe_h26xf_stream_ue(s);
        uint32_t crop_top = upipe_h26xf_stream_ue(s);
        uint32_t crop_bottom = upipe_h26xf_stream_ue(s);
        uint8_t chroma_array_type = 0;
        if (!upipe_h264f->separate_colour_plane)
            chroma_array_type = chroma_idc;
        if (!chroma_array_type) {
            hsize -= crop_left + crop_right;
            vsize -= (crop_top + crop_bottom) *
                (upipe_h264f->frame_mbs_only ? 1 : 2);
        } else {
            hsize -= (crop_left + crop_right) *
                ((chroma_idc == 1 || chroma_idc == 2) ? 2 : 1);
            vsize -= (crop_top + crop_bottom) * (chroma_idc == 1 ? 2 : 1) *
                (upipe_h264f->frame_mbs_only ? 1 : 2);
        }
    }
    UBASE_FATAL(upipe, uref_pic_flow_set_hsize(flow_def, hsize))
    UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def, vsize))

    upipe_h26xf_stream_fill_bits(s, 1);
    bool vui = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    uint8_t video_format = 5;
    bool full_range = false;
    uint8_t colour_primaries = 2;
    uint8_t transfer_characteristics = 2;
    uint8_t matrix_coefficients = 2;

    if (vui) {
        upipe_h26xf_stream_fill_bits(s, 1);
        bool ar_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (ar_present) {
            upipe_h26xf_stream_fill_bits(s, 8);
            uint8_t ar_idc = ubuf_block_stream_show_bits(s, 8);
            ubuf_block_stream_skip_bits(s, 8);
            if (ar_idc > 0 &&
                ar_idc < sizeof(upipe_h26xf_sar_from_idc) / sizeof(struct urational)) {
                UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def,
                            upipe_h26xf_sar_from_idc[ar_idc]));
            } else if (ar_idc == H264VUI_AR_EXTENDED) {
                struct urational sar;
                upipe_h26xf_stream_fill_bits(s, 16);
                sar.num = ubuf_block_stream_show_bits(s, 16);
                ubuf_block_stream_skip_bits(s, 16);
                upipe_h26xf_stream_fill_bits(s, 16);
                sar.den = ubuf_block_stream_show_bits(s, 16);
                ubuf_block_stream_skip_bits(s, 16);
                urational_simplify(&sar);
                UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def, sar))
            } else
                upipe_warn_va(upipe, "unknown aspect ratio idc %"PRIu8, ar_idc);
        }

        upipe_h26xf_stream_fill_bits(s, 3);
        bool overscan_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (overscan_present) {
            UBASE_FATAL(upipe, uref_pic_flow_set_overscan(flow_def,
                        !!ubuf_block_stream_show_bits(s, 1)))
            ubuf_block_stream_skip_bits(s, 1);
        }

        bool video_signal_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (video_signal_present) {
            upipe_h26xf_stream_fill_bits(s, 5);
            video_format = ubuf_block_stream_show_bits(s, 3);
            ubuf_block_stream_skip_bits(s, 3);
            full_range = !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            bool colour_present = !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            if (colour_present) {
                upipe_h26xf_stream_fill_bits(s, 24);
                colour_primaries = ubuf_block_stream_show_bits(s, 8);
                ubuf_block_stream_skip_bits(s, 8);
                transfer_characteristics = ubuf_block_stream_show_bits(s, 8);
                ubuf_block_stream_skip_bits(s, 8);
                matrix_coefficients = ubuf_block_stream_show_bits(s, 8);
                ubuf_block_stream_skip_bits(s, 8);
            }
        }

        upipe_h26xf_stream_fill_bits(s, 1);
        bool chroma_loc_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (chroma_loc_present) {
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_ue(s);
        }

        upipe_h26xf_stream_fill_bits(s, 1);
        bool timing_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (timing_present) {
            upipe_h26xf_stream_fill_bits(s, 24);
            uint32_t num_units_in_ticks =
                ubuf_block_stream_show_bits(s, 24) << 8;
            ubuf_block_stream_skip_bits(s, 24);

            upipe_h26xf_stream_fill_bits(s, 24);
            num_units_in_ticks |= ubuf_block_stream_show_bits(s, 8);
            ubuf_block_stream_skip_bits(s, 8);
            uint32_t time_scale = ubuf_block_stream_show_bits(s, 16) << 16;
            ubuf_block_stream_skip_bits(s, 16);

            upipe_h26xf_stream_fill_bits(s, 17);
            time_scale |= ubuf_block_stream_show_bits(s, 16);
            ubuf_block_stream_skip_bits(s, 16);

            bool fixed_frame_rate = ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);

            if (time_scale && num_units_in_ticks) {
                struct urational frame_rate = {
                    .num = time_scale,
                    .den = num_units_in_ticks * 2
                };
                urational_simplify(&frame_rate);
                if (fixed_frame_rate) {
                    UBASE_FATAL(upipe,
                            uref_pic_flow_set_fps(flow_def, frame_rate))
                }
                upipe_h264f->duration = UCLOCK_FREQ * frame_rate.den /
                                        frame_rate.num / 2;
                UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                            upipe_h264f->input_latency +
                            upipe_h264f->duration * 2))
            }

            if (!fixed_frame_rate)
                upipe_warn(upipe, "VFR stream");
        }

        uint64_t octetrate, cpb_size;
        upipe_h26xf_stream_fill_bits(s, 1);
        bool nal_hrd_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (nal_hrd_present) {
            if (!ubase_check(upipe_h264f_stream_parse_hrd(upipe, s, &octetrate,
                                                          &cpb_size))) {
                ubuf_block_stream_clean(s);
                uref_free(flow_def);
                return false;
            }
            UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def, octetrate))
            UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def, cpb_size))
            upipe_h264f->octet_rate = octetrate;
        }

        upipe_h26xf_stream_fill_bits(s, 1);
        bool vcl_hrd_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (vcl_hrd_present) {
            if (!ubase_check(upipe_h264f_stream_parse_hrd(upipe, s, &octetrate,
                                                          &cpb_size))) {
                ubuf_block_stream_clean(s);
                uref_free(flow_def);
                return false;
            }
            UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def, octetrate))
            UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def, cpb_size))
        }

        if (nal_hrd_present || vcl_hrd_present) {
            upipe_h26xf_stream_fill_bits(s, 1);
            if (!!ubuf_block_stream_show_bits(s, 1))
                UBASE_FATAL(upipe, uref_flow_set_lowdelay(flow_def))
            ubuf_block_stream_skip_bits(s, 1);
            upipe_h264f->hrd = true;
        } else
            upipe_h264f->hrd = false;

        upipe_h26xf_stream_fill_bits(s, 2);
        upipe_h264f->pic_struct_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        bool bitstream_restriction = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);

        if (bitstream_restriction) {
            upipe_h26xf_stream_fill_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_ue(s);
            upipe_h264f->max_dec_frame_buffering = upipe_h26xf_stream_ue(s);
        }
    } else {
        upipe_h264f->duration = 0;
        upipe_h264f->pic_struct_present = false;
        upipe_h264f->hrd = false;
        upipe_h264f->max_dec_frame_buffering = UINT32_MAX;
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

    if (full_range) {
        UBASE_FATAL(upipe, uref_pic_flow_set_full_range(flow_def))
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
        case 8:
            colour_primaries_str = "film";
            break;
        case 9:
            colour_primaries_str = "bt2020";
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
        case 9:
            transfer_characteristics_str = "log100";
            break;
        case 10:
            transfer_characteristics_str = "log316";
            break;
        case 11:
            transfer_characteristics_str = "iec61966-2-4";
            break;
        case 12:
            transfer_characteristics_str = "bt1361e";
            break;
        case 13:
            transfer_characteristics_str = "iec61966-2-1";
            break;
        case 14:
            transfer_characteristics_str = "bt2020-10";
            break;
        case 15:
            transfer_characteristics_str = "bt2020-12";
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
        case 0:
            matrix_coefficients_str = "GBR";
            break;
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
        case 8:
            matrix_coefficients_str = "YCgCo";
            break;
        case 9:
            matrix_coefficients_str = "bt2020nc";
            break;
        case 10:
            matrix_coefficients_str = "bt2020c";
            break;
        default:
            break;
    }
    if (matrix_coefficients_str != NULL) {
        UBASE_FATAL(upipe, uref_pic_flow_set_matrix_coefficients(flow_def,
                    matrix_coefficients_str))
    }

    upipe_h264f->active_sps = sps_id;
    ubuf_block_stream_clean(s);

    upipe_h264f_store_flow_def(upipe, NULL);
    uref_free(upipe_h264f->flow_def_requested);
    upipe_h264f->flow_def_requested = NULL;
    flow_def = upipe_h264f_store_flow_def_attr(upipe, flow_def);
    if (flow_def != NULL)
        upipe_h264f_require_flow_format(upipe, flow_def);
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

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, upipe_h264f->pps[pps_id], 1))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    upipe_h26xf_stream_ue(s); /* pps_id */
    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32, sps_id);
        ubuf_block_stream_clean(s);
        return false;
    }

    if (!upipe_h264f_activate_sps(upipe, sps_id)) {
        ubuf_block_stream_clean(s);
        return false;
    }

    upipe_h26xf_stream_fill_bits(s, 2);
    ubuf_block_stream_skip_bits(s, 1);
    upipe_h264f->bf_poc = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);

    upipe_h264f->active_pps = pps_id;
    ubuf_block_stream_clean(s);
    return true;
}

/** @internal @This handles a sequence parameter set.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @return an error code
 */
static int upipe_h264f_handle_sps(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_h264f->sps_rap = upipe_h264f->dts_rap;

    ubuf = ubuf_block_splice(ubuf, offset, size);
    if (unlikely(ubuf == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, ubuf,
                                            H264SPS_HEADER_SIZE - 3))) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }
    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32, sps_id);
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }

    if (upipe_h264f->active_sps == sps_id &&
        !ubase_check(ubuf_block_equal(upipe_h264f->sps[sps_id], ubuf)))
        upipe_h264f->active_sps = -1;

    if (upipe_h264f->sps[sps_id] != NULL)
        ubuf_free(upipe_h264f->sps[sps_id]);
    upipe_h264f->sps[sps_id] = ubuf;
    return UBASE_ERR_NONE;
}

/** @internal @This handles a sequence parameter set extension.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @return an error code
 */
static int upipe_h264f_handle_sps_ext(struct upipe *upipe, struct ubuf *ubuf,
                                      size_t offset, size_t size)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    ubuf = ubuf_block_splice(ubuf, offset, size);
    if (unlikely(ubuf == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, ubuf, 1))) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }
    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS extension %"PRIu32, sps_id);
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }

    /* We do not reset active_sps because so far we don't care about SPS
     * extension. */

    if (upipe_h264f->sps_ext[sps_id] != NULL)
        ubuf_free(upipe_h264f->sps_ext[sps_id]);
    upipe_h264f->sps_ext[sps_id] = ubuf;
    return UBASE_ERR_NONE;
}

/** @internal @This handles a picture parameter set.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @return an error code
 */
static int upipe_h264f_handle_pps(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    upipe_h264f->pps_rap = upipe_h264f->sps_rap;

    ubuf = ubuf_block_splice(ubuf, offset, size);
    if (unlikely(ubuf == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, ubuf, 1))) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }
    uint32_t pps_id = upipe_h26xf_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(pps_id >= H264PPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid PPS %"PRIu32, pps_id);
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }

    if (upipe_h264f->active_sps == -1 ||
        (upipe_h264f->active_pps == pps_id &&
         !ubase_check(ubuf_block_equal(upipe_h264f->pps[pps_id], ubuf)))) {
        upipe_h264f->active_pps = -1;
    }

    if (upipe_h264f->pps[pps_id] != NULL)
        ubuf_free(upipe_h264f->pps[pps_id]);
    upipe_h264f->pps[pps_id] = ubuf;
    return UBASE_ERR_NONE;
}

/** @internal @This handles the supplemental enhancement information called
 * buffering period.
 *
 * @param upipe description structure of the pipe
 * @param s block stream parsing structure
 * @return an error code
 */
static int upipe_h264f_handle_sei_buffering_period(struct upipe *upipe,
                                                   struct ubuf_block_stream *s)
{
    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    if (unlikely(sps_id >= H264SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32" in SEI", sps_id);
        return UBASE_ERR_INVALID;
    }

    if (!upipe_h264f_activate_sps(upipe, sps_id))
        return UBASE_ERR_INVALID;
    return UBASE_ERR_NONE;
}

/** @internal @This handles the supplemental enhancement information called
 * picture timing.
 *
 * @param upipe description structure of the pipe
 * @param s block stream parsing structure
 * @return an error code
 */
static int upipe_h264f_handle_sei_pic_timing(struct upipe *upipe,
                                             struct ubuf_block_stream *s)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (unlikely(upipe_h264f->active_sps == -1)) {
        upipe_warn(upipe, "discarding early picture timing SEI");
        return UBASE_ERR_NONE;
    }

    if (upipe_h264f->hrd) {
        size_t cpb_removal_delay_length =
            upipe_h264f->cpb_removal_delay_length;
        while (cpb_removal_delay_length > 24) {
            upipe_h26xf_stream_fill_bits(s, 24);
            ubuf_block_stream_skip_bits(s, 24);
            cpb_removal_delay_length -= 24;
        }
        upipe_h26xf_stream_fill_bits(s, cpb_removal_delay_length);
        ubuf_block_stream_skip_bits(s, cpb_removal_delay_length);

        size_t dpb_output_delay_length =
            upipe_h264f->dpb_output_delay_length;
        uint64_t dpb_output_delay = 0;
        while (dpb_output_delay_length > 24) {
            dpb_output_delay <<= 24;
            upipe_h26xf_stream_fill_bits(s, 24);
            dpb_output_delay |= ubuf_block_stream_show_bits(s, 24);
            ubuf_block_stream_skip_bits(s, 24);
            dpb_output_delay_length -= 24;
        }
        dpb_output_delay <<= dpb_output_delay_length;
        upipe_h26xf_stream_fill_bits(s, dpb_output_delay_length);
        dpb_output_delay |=
            ubuf_block_stream_show_bits(s, dpb_output_delay_length);
        ubuf_block_stream_skip_bits(s, dpb_output_delay_length);
        upipe_h264f->dpb_output_delay = dpb_output_delay;
    }

    if (upipe_h264f->pic_struct_present) {
        upipe_h26xf_stream_fill_bits(s, 4);
        upipe_h264f->pic_struct = ubuf_block_stream_show_bits(s, 4);
        ubuf_block_stream_skip_bits(s, 4);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This handles a supplemental enhancement information.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @return an error code
 */
static int upipe_h264f_handle_sei(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint8_t type;
    if (unlikely(!ubase_check(ubuf_block_extract(ubuf, offset + 1, 1, &type))))
        return UBASE_ERR_INVALID;
    if (type != H264SEI_BUFFERING_PERIOD && type != H264SEI_PIC_TIMING)
        return UBASE_ERR_NONE;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    UBASE_RETURN(ubuf_block_stream_init(s, ubuf, offset + 2))

    /* size field */
    uint8_t octet;
    do {
        upipe_h26xf_stream_fill_bits(s, 8);
        octet = ubuf_block_stream_show_bits(s, 8);
        ubuf_block_stream_skip_bits(s, 8);
    } while (octet == UINT8_MAX);

    int err = UBASE_ERR_NONE;
    switch (type) {
        case H264SEI_BUFFERING_PERIOD:
            err = upipe_h264f_handle_sei_buffering_period(upipe, s);
            break;
        case H264SEI_PIC_TIMING:
            err = upipe_h264f_handle_sei_pic_timing(upipe, s);
            break;
        default:
            break;
    }

    ubuf_block_stream_clean(s);
    return err;
}

/** @internal @This parses and handles a slice NAL.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @param nal nal header
 * @param au_slice_p true if a slice NAL has already been parsed for this AU
 * @return an error code, esp. UBASE_ERR_BUSY if the previous NAL needs output
 */
static int upipe_h264f_handle_slice(struct upipe *upipe, struct ubuf *ubuf,
                                    size_t offset, size_t size, uint8_t nal,
                                    bool *au_slice_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (unlikely(!ubase_check(ubuf_block_stream_init(s, ubuf, offset + 1))))
        return UBASE_ERR_INVALID;

    upipe_h26xf_stream_ue(s); /* first_mb_in_slice */
    uint32_t slice_type = upipe_h26xf_stream_ue(s);
    uint32_t pps_id = upipe_h26xf_stream_ue(s);
    if (unlikely(pps_id >= H264PPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid PPS %"PRIu32" in slice", pps_id);
        ubuf_block_stream_clean(s);
        return UBASE_ERR_INVALID;
    }

    if (*au_slice_p && pps_id != upipe_h264f->active_pps) {
        ubuf_block_stream_clean(s);
        return UBASE_ERR_BUSY;
    }

    if (unlikely(!upipe_h264f_activate_pps(upipe, pps_id))) {
        ubuf_block_stream_clean(s);
        return UBASE_ERR_INVALID;
    }

    if (upipe_h264f->separate_colour_plane) {
        upipe_h26xf_stream_fill_bits(s, 2);
        ubuf_block_stream_skip_bits(s, 2);
    }
    upipe_h26xf_stream_fill_bits(s, upipe_h264f->log2_max_frame_num);
    uint32_t frame_num = ubuf_block_stream_show_bits(s,
            upipe_h264f->log2_max_frame_num);
    ubuf_block_stream_skip_bits(s, upipe_h264f->log2_max_frame_num);
    bool field_pic = false;
    bool bf = false;
    if (!upipe_h264f->frame_mbs_only) {
        upipe_h26xf_stream_fill_bits(s, 2);
        field_pic = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (field_pic) {
            bf = !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }
    }

    uint32_t idr_pic_id = upipe_h264f->idr_pic_id;
    if (h264nalst_get_type(nal) == H264NAL_TYPE_IDR)
        idr_pic_id = upipe_h26xf_stream_ue(s);

    if (*au_slice_p &&
        (frame_num != upipe_h264f->frame_num ||
         field_pic != upipe_h264f->field_pic ||
         bf != upipe_h264f->bf ||
         idr_pic_id != upipe_h264f->idr_pic_id)) {
        ubuf_block_stream_clean(s);
        return UBASE_ERR_BUSY;
    }
    upipe_h264f->frame_num = frame_num;
    upipe_h264f->slice_type = slice_type;
    upipe_h264f->field_pic = field_pic;
    upipe_h264f->bf = bf;
    upipe_h264f->idr_pic_id = idr_pic_id;

    if (upipe_h264f->poc_type == 0) {
        upipe_h26xf_stream_fill_bits(s, upipe_h264f->log2_max_poc_lsb);
        uint32_t poc_lsb = ubuf_block_stream_show_bits(s,
                upipe_h264f->log2_max_poc_lsb);
        ubuf_block_stream_skip_bits(s, upipe_h264f->log2_max_poc_lsb);
        int32_t delta_poc_bottom = 0;
        if (upipe_h264f->bf_poc && !field_pic)
            delta_poc_bottom = upipe_h26xf_stream_se(s);

        if (*au_slice_p &&
            (poc_lsb != upipe_h264f->poc_lsb ||
             delta_poc_bottom != upipe_h264f->delta_poc_bottom)) {
            ubuf_block_stream_clean(s);
            return UBASE_ERR_BUSY;
        }
        upipe_h264f->poc_lsb = poc_lsb;
        upipe_h264f->delta_poc_bottom = delta_poc_bottom;

    } else if (upipe_h264f->poc_type == 1 &&
               !upipe_h264f->delta_poc_always_zero) {
        int32_t delta_poc0 = upipe_h26xf_stream_se(s);
        int32_t delta_poc1 = 0;
        if (upipe_h264f->bf_poc && !field_pic)
            delta_poc1 = upipe_h26xf_stream_se(s);

        if (*au_slice_p &&
            (delta_poc0 != upipe_h264f->delta_poc0 ||
             delta_poc1 != upipe_h264f->delta_poc1)) {
            ubuf_block_stream_clean(s);
            return UBASE_ERR_BUSY;
        }
        upipe_h264f->delta_poc0 = delta_poc0;
        upipe_h264f->delta_poc1 = delta_poc1;
    }

    *au_slice_p = true;
    return UBASE_ERR_NONE;
}

/** @internal @This parses and handles a NAL unit.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @param au_slice_p true if a slice NAL has already been parsed for this AU
 * @param nal_p filled in with the nal header if not NULL
 * @return an error code
 */
static int upipe_h264f_handle_nal(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size, bool *au_slice_p,
                                  uint8_t *nal_p)
{
    uint8_t nal;
    if (unlikely(!ubase_check(ubuf_block_extract(ubuf, offset, 1, &nal))))
        return UBASE_ERR_INVALID;
    if (nal_p != NULL)
        *nal_p = nal;

    upipe_verbose_va(upipe, "handling NAL %"PRIu8, h264nalst_get_type(nal));
    switch (h264nalst_get_type(nal)) {
        case H264NAL_TYPE_SEI:
            return upipe_h264f_handle_sei(upipe, ubuf, offset, size);
        case H264NAL_TYPE_SPS:
            return upipe_h264f_handle_sps(upipe, ubuf, offset, size);
        case H264NAL_TYPE_SPSX:
            return upipe_h264f_handle_sps_ext(upipe, ubuf, offset, size);
        case H264NAL_TYPE_PPS:
            return upipe_h264f_handle_pps(upipe, ubuf, offset, size);
        case H264NAL_TYPE_NONIDR:
        case H264NAL_TYPE_PARTA:
        case H264NAL_TYPE_PARTB:
        case H264NAL_TYPE_PARTC:
        case H264NAL_TYPE_IDR:
            return upipe_h264f_handle_slice(upipe, ubuf, offset, size, nal,
                                            au_slice_p);
        default:
            break;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This uses annex B global headers.
 *
 * @param upipe description structure of the pipe
 * @param p pointer to global headers
 * @param size size of global headers
 */
static void upipe_h264f_handle_global_annexb(struct upipe *upipe,
                                             const uint8_t *p, size_t size)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (upipe_h264f->encaps_input != UREF_H26X_ENCAPS_ANNEXB &&
        upipe_h264f->encaps_input != UREF_H26X_ENCAPS_NALU) {
        upipe_warn_va(upipe,
                      "fixing up input encapsulation to annex B (from %d)",
                      upipe_h264f->encaps_input);
        upipe_h264f->encaps_input = UREF_H26X_ENCAPS_ANNEXB;
    }

    int b = 0;
    bool au_slice = false;
    do {
        int e = b + 3;
        while (e < size - 3 &&
               (p[e] != 0 || p[e + 1] != 0 || p[e + 2] != 1))
            e++;
        if (e == size - 3)
            e = size;
        else if (p[e - 1] == 0)
            e--;

        int s = p[b + 2] == 0 ? 4 : 3;

        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_h264f->ubuf_mgr,
                                                         p + b + s, e - b - s);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_verbose_va(upipe, "found global header NAL %"PRIu8" of size %d",
                         h264nalst_get_type(p[b + s]), e - b - s);

        int err = upipe_h264f_handle_nal(upipe, ubuf, 0, e - b - s, &au_slice,
                                         NULL);
        if (!ubase_check(err)) {
            upipe_err(upipe, "invalid global NAL received");
            upipe_throw_fatal(upipe, err);
        }
        ubuf_free(ubuf);

        b = e;
    } while (b < size - 3);
}

/** @internal @This uses avcC global headers.
 *
 * @param upipe description structure of the pipe
 * @param p pointer to global headers
 * @param size size of global headers
 */
static void upipe_h264f_handle_global_avcc(struct upipe *upipe,
                                           const uint8_t *p, size_t size)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (!h264avcc_validate(p, size)) {
        upipe_err_va(upipe, "invalid avcC structure (size %zu)", size);
        return;
    }

    uint8_t length_size = h264avcc_get_length_size_1(p) + 1;
    enum uref_h26x_encaps encaps_input;
    switch (length_size) {
        case 1: encaps_input = UREF_H26X_ENCAPS_LENGTH1; break;
        case 2: encaps_input = UREF_H26X_ENCAPS_LENGTH2; break;
        case 4: encaps_input = UREF_H26X_ENCAPS_LENGTH4; break;
        default:
            upipe_err(upipe, "invalid length size in avcC");
            return;
    }

    if (upipe_h264f->encaps_input != encaps_input) {
        upipe_warn_va(upipe,
                "fixing up input encapsulation to length%"PRIu8" (from %d)",
                length_size, upipe_h264f->encaps_input);
        upipe_h264f->encaps_input = encaps_input;
    }

    bool au_slice = false;
    uint8_t nb = h264avcc_get_nb_sps(p);
    for (uint8_t n = 0; n < nb; n++) {
        const uint8_t *ph = h264avcc_get_spsh(p, n);
        uint16_t length = h264avcc_spsh_get_length(ph);
        const uint8_t *sps = h264avcc_spsh_get_sps(ph);

        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_h264f->ubuf_mgr,
                                                         sps, length);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        int err = upipe_h264f_handle_nal(upipe, ubuf, 0, length, &au_slice,
                                         NULL);
        if (!ubase_check(err)) {
            upipe_err(upipe, "invalid global NAL received");
            upipe_throw_fatal(upipe, err);
        }
        ubuf_free(ubuf);
    }

    nb = h264avcc_get_nb_pps(p);
    for (uint8_t n = 0; n < nb; n++) {
        const uint8_t *ph = h264avcc_get_ppsh(p, n);
        uint16_t length = h264avcc_ppsh_get_length(ph);
        const uint8_t *pps = h264avcc_ppsh_get_pps(ph);

        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_h264f->ubuf_mgr,
                                                         pps, length);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        int err = upipe_h264f_handle_nal(upipe, ubuf, 0, length, &au_slice,
                                         NULL);
        if (!ubase_check(err)) {
            upipe_err(upipe, "invalid global NAL received");
            upipe_throw_fatal(upipe, err);
        }
        ubuf_free(ubuf);
    }
}

/** @internal @This checks if there are global headers and uses them.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_handle_global(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    const uint8_t *p;
    size_t size;
    if (!ubase_check(uref_flow_get_headers(upipe_h264f->flow_def_input,
                                           &p, &size)) || size < 5)
        return;

    if (p[0] == 0)
        upipe_h264f_handle_global_annexb(upipe, p, size);
    else
        upipe_h264f_handle_global_avcc(upipe, p, size);
}

/** @internal @This builds the global headers of the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 */
static void upipe_h264f_build_global(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    size_t headers_size = 0;
    unsigned int nb_sps = 0;
    unsigned int nb_pps = 0;
    int i;
    for (i = 0; i < H264SPS_ID_MAX; i++) {
        if (upipe_h264f->sps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h264f->sps[i], &ubuf_size);
            headers_size += ubuf_size;
            nb_sps++;
        }
    }
    for (i = 0; i < H264PPS_ID_MAX; i++) {
        if (upipe_h264f->pps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h264f->pps[i], &ubuf_size);
            headers_size += ubuf_size;
            nb_pps++;
        }
    }

    if (upipe_h264f->encaps_output == UREF_H26X_ENCAPS_ANNEXB) {
        uint8_t headers[headers_size + nb_sps * 4 + nb_pps * 4];
        int j = 0;
        for (i = 0; i < H264SPS_ID_MAX; i++) {
            if (upipe_h264f->sps[i] != NULL) {
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 1;
                size_t ubuf_size = 0;
                ubuf_block_size(upipe_h264f->sps[i], &ubuf_size);
                UBASE_FATAL(upipe,
                        ubuf_block_extract(upipe_h264f->sps[i], 0, -1,
                                           headers + j))
                j += ubuf_size;
            }
        }
        for (i = 0; i < H264PPS_ID_MAX; i++) {
            if (upipe_h264f->pps[i] != NULL) {
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 1;
                size_t ubuf_size = 0;
                ubuf_block_size(upipe_h264f->pps[i], &ubuf_size);
                UBASE_FATAL(upipe,
                        ubuf_block_extract(upipe_h264f->pps[i], 0, -1,
                                           headers + j))
                j += ubuf_size;
            }
        }
        UBASE_FATAL(upipe, uref_flow_set_headers(flow_def, headers, j))
        return;
    }

    /* build avcC */
    uint8_t length_size;
    switch (upipe_h264f->encaps_output) {
        case UREF_H26X_ENCAPS_LENGTH1: length_size = 1; break;
        case UREF_H26X_ENCAPS_LENGTH2: length_size = 2; break;
        default:
        case UREF_H26X_ENCAPS_LENGTH4: length_size = 4; break;
    }

    uint8_t headers[H264AVCC_HEADER + H264AVCC_HEADER2 + headers_size +
                    nb_sps * H264AVCC_SPS_HEADER +
                    nb_pps * H264AVCC_PPS_HEADER];
    h264avcc_init(headers);
    h264avcc_set_profile(headers, upipe_h264f->profile);
    h264avcc_set_profile_compatibility(headers,
                                       upipe_h264f->profile_compatibility);
    h264avcc_set_level(headers, upipe_h264f->level);
    h264avcc_set_length_size_1(headers, length_size - 1);

    h264avcc_set_nb_sps(headers, nb_sps);
    uint8_t n = 0;
    for (i = 0; i < H264SPS_ID_MAX; i++) {
        if (upipe_h264f->sps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h264f->sps[i], &ubuf_size);
            assert(ubuf_size <= UINT16_MAX);
            uint8_t *p = h264avcc_get_spsh(headers, n++);
            h264avcc_spsh_set_length(p, ubuf_size);
            p = h264avcc_spsh_get_sps(p);
            UBASE_FATAL(upipe,
                    ubuf_block_extract(upipe_h264f->sps[i], 0, -1, p))
        }
    }

    h264avcc_set_nb_pps(headers, nb_pps);
    n = 0;
    for (i = 0; i < H264PPS_ID_MAX; i++) {
        if (upipe_h264f->pps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h264f->pps[i], &ubuf_size);
            assert(ubuf_size <= UINT16_MAX);
            uint8_t *p = h264avcc_get_ppsh(headers, n++);
            h264avcc_ppsh_set_length(p, ubuf_size);
            p = h264avcc_ppsh_get_pps(p);
            UBASE_FATAL(upipe,
                    ubuf_block_extract(upipe_h264f->pps[i], 0, -1, p))
        }
    }

    uint8_t *p = h264avcc_get_ppsh(headers, n);
    UBASE_FATAL(upipe, uref_flow_set_headers(flow_def, headers, p - headers))
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_build_flow_def(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    assert(upipe_h264f->flow_def_requested != NULL);

    struct uref *flow_def = uref_dup(upipe_h264f->flow_def_requested);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    const uint8_t *p;
    size_t size;
    if (!ubase_check(uref_flow_get_global(flow_def)))
        uref_flow_delete_headers(flow_def);
    else if (!ubase_check(uref_flow_get_headers(flow_def, &p, &size)) ||
             upipe_h264f->encaps_input != upipe_h264f->encaps_output)
        upipe_h264f_build_global(upipe, flow_def);

    if (upipe_h264f->duration) {
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                    upipe_h264f->input_latency + upipe_h264f->duration * 2))
    }

    upipe_h264f_store_flow_def(upipe, flow_def);
    /* force sending flow definition immediately */
    upipe_h264f_output(upipe, NULL, NULL);
}

/** @internal @This prepares an access unit for output.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @return an error code
 */
static int upipe_h264f_prepare_au(struct upipe *upipe, struct uref *uref)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint64_t picture_number = upipe_h264f->last_picture_number +
        (upipe_h264f->frame_num - upipe_h264f->last_frame_num);
    if (upipe_h264f->frame_num > upipe_h264f->last_frame_num) {
        upipe_h264f->last_frame_num = upipe_h264f->frame_num;
        upipe_h264f->last_picture_number = picture_number;
    }
    UBASE_RETURN(uref_pic_set_number(uref, picture_number))

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
            UBASE_RETURN(uref_pic_set_progressive(uref))
            duration *= 2;
            break;
        case H264SEI_STRUCT_TOP:
            UBASE_RETURN(uref_pic_set_tf(uref))
            break;
        case H264SEI_STRUCT_BOT:
            UBASE_RETURN(uref_pic_set_bf(uref))
            break;
        case H264SEI_STRUCT_TOP_BOT:
            UBASE_RETURN(uref_pic_set_tf(uref))
            UBASE_RETURN(uref_pic_set_bf(uref))
            UBASE_RETURN(uref_pic_set_tff(uref))
            duration *= 2;
            break;
        case H264SEI_STRUCT_BOT_TOP:
            UBASE_RETURN(uref_pic_set_tf(uref))
            UBASE_RETURN(uref_pic_set_bf(uref))
            duration *= 2;
            break;
        case H264SEI_STRUCT_TOP_BOT_TOP:
            UBASE_RETURN(uref_pic_set_tf(uref))
            UBASE_RETURN(uref_pic_set_bf(uref))
            UBASE_RETURN(uref_pic_set_tff(uref))
            duration *= 3;
            break;
        case H264SEI_STRUCT_BOT_TOP_BOT:
            UBASE_RETURN(uref_pic_set_tf(uref))
            UBASE_RETURN(uref_pic_set_bf(uref))
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
    if (duration) {
        UBASE_RETURN(uref_clock_set_duration(uref, duration))
    }

    UBASE_RETURN(uref_h264_set_type(uref, upipe_h264f->slice_type % 5))
    switch (upipe_h264f->slice_type % 5) {
        case H264SLI_TYPE_I:
            upipe_h264f->iframe_rap = upipe_h264f->pps_rap;
            UBASE_RETURN(uref_pic_set_key(uref))
            break;
        default:
            break;
    }

    if (upipe_h264f->iframe_rap != UINT64_MAX)
        if (!ubase_check(uref_clock_set_rap_sys(uref, upipe_h264f->iframe_rap)))
            upipe_warn_va(upipe, "couldn't set rap_sys");
    return UBASE_ERR_NONE;
}

/** @internal @This finds a NAL in an annex B uref.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @param nal_type type of NAL
 * @return offset of the NAL, or -1 if not found
 */
static int64_t upipe_h264f_find_annexb_nal(struct upipe *upipe,
                                           struct uref *uref, uint8_t nal_type)
{
    uint64_t nal_units = 0;
    uint64_t nal_offset = 0;
    uint64_t nal_size = 0;
    while (ubase_check(uref_h26x_iterate_nal(uref, &nal_units,
                                             &nal_offset, &nal_size, 0))) {
        uint8_t startcode[5];

        if (!ubase_check(uref_block_extract(uref, nal_offset, 5, startcode)))
            return -1;

        uint8_t nal;
        if (startcode[2] == 1)
            nal = h264nalst_get_type(startcode[3]);
        else
            nal = h264nalst_get_type(startcode[4]);
        if (nal == nal_type)
            return nal_offset;
    }

    return -1;
}

/** @internal @This outputs an access unit.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h264f_output_au(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);

    int err = upipe_h26xf_convert_frame(uref,
            upipe_h264f->encaps_input, upipe_h264f->encaps_output,
            upipe_h264f->ubuf_mgr, upipe_h264f->annexb_header);
    if (!ubase_check(err)) {
        upipe_throw_error(upipe, err);
        uref_free(uref);
        return;
    }

    if (upipe_h264f->encaps_output != UREF_H26X_ENCAPS_ANNEXB) {
        upipe_h264f_output(upipe, uref, upump_p);
        return;
    }

    if (ubase_check(uref_pic_get_key(uref)) &&
        upipe_h264f_find_annexb_nal(upipe, uref, H264NAL_TYPE_SPS) == -1) {
        upipe_verbose(upipe, "prepending SPS and PPS on keyframe");

        struct ubuf *ubuf = ubuf_dup(upipe_h264f->annexb_header);
        struct ubuf *ubuf2 =
            ubuf_dup(upipe_h264f->pps[upipe_h264f->active_pps]);
        if (unlikely(ubuf == NULL || ubuf2 == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            ubuf_free(ubuf);
            ubuf_free(ubuf2);
            uref_free(uref);
            return;
        }
        ubuf_block_append(ubuf, ubuf2);
        int err = uref_h26x_prepend_nal(uref, ubuf);
        if (unlikely(!ubase_check(err)))
            upipe_throw_error(upipe, err);

        ubuf = ubuf_dup(upipe_h264f->annexb_header);
        ubuf2 = ubuf_dup(upipe_h264f->sps[upipe_h264f->active_sps]);
        if (unlikely(ubuf == NULL || ubuf2 == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            ubuf_free(ubuf);
            ubuf_free(ubuf2);
            uref_free(uref);
            return;
        }
        ubuf_block_append(ubuf, ubuf2);
        err = uref_h26x_prepend_nal(uref, ubuf);
        if (unlikely(!ubase_check(err)))
            upipe_throw_error(upipe, err);
    }

    if (upipe_h264f_find_annexb_nal(upipe, uref, H264NAL_TYPE_AUD) == -1) {
        upipe_verbose(upipe, "prepending AUD");
        struct ubuf *ubuf = ubuf_dup(upipe_h264f->annexb_aud);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        int err = uref_h26x_prepend_nal(uref, ubuf);
        if (unlikely(!ubase_check(err)))
            upipe_throw_error(upipe, err);
    }

    upipe_h264f_output(upipe, uref, upump_p);
}

/** @internal @This prepares an annex B access unit.
 *
 * @param upipe description structure of the pipe
 * @return pointer to uref
 */
static struct uref *upipe_h264f_prepare_annexb(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (!upipe_h264f->au_size) {
        upipe_h264f->au_nal_units = 0;
        upipe_h264f->au_vcl_offset = -1;
        upipe_h264f->au_slice = false;
        upipe_h264f->au_slice_nal = UINT8_MAX;
        upipe_h264f->pic_struct = -1;
        upipe_h264f->dpb_output_delay = UINT64_MAX;
        return NULL;
    }
    if (upipe_h264f->au_slice_nal == UINT8_MAX ||
        upipe_h264f->active_sps == -1 || upipe_h264f->active_pps == -1) {
        upipe_warn(upipe, "discarding data without SPS/PPS");
        upipe_h264f_consume_uref_stream(upipe, upipe_h264f->au_size);
        upipe_h264f->au_size = 0;
        upipe_h264f->au_nal_units = 0;
        upipe_h264f->au_vcl_offset = -1;
        upipe_h264f->au_slice = false;
        upipe_h264f->au_slice_nal = UINT8_MAX;
        upipe_h264f->pic_struct = -1;
        upipe_h264f->dpb_output_delay = UINT64_MAX;
        return NULL;
    }

    struct uref au_uref_s = upipe_h264f->au_uref_s;
    struct urational drift_rate = upipe_h264f->drift_rate;
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_h264f_flush_dates(upipe);

    struct uref *uref = upipe_h264f_extract_uref_stream(upipe,
                                                        upipe_h264f->au_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }
    upipe_h264f->au_nal_units = 0;

    int err = upipe_h264f_prepare_au(upipe, uref);
    UBASE_FATAL(upipe, err);

    if (upipe_h264f->au_vcl_offset > 0)
        UBASE_FATAL(upipe, uref_block_set_header_size(uref,
                    upipe_h264f->au_vcl_offset))

    uint64_t duration = 0;
    uref_clock_get_duration(uref, &duration);

    uint64_t date;
    if (!ubase_check(uref_clock_get_dts_prog(&au_uref_s, &date)) &&
            upipe_h264f->dpb_output_delay != UINT64_MAX) {
        uref_clock_set_dts_pts_delay(&au_uref_s,
                upipe_h264f->dpb_output_delay * upipe_h264f->duration);
        uref_clock_set_dts_pts_delay(uref,
                upipe_h264f->dpb_output_delay * upipe_h264f->duration);
    } else if (ubase_check(uref_clock_get_dts_pts_delay(&au_uref_s, &date)))
        uref_clock_set_dts_pts_delay(uref, date);
    else if (upipe_h264f->max_dec_frame_buffering != UINT32_MAX &&
             ubase_check(uref_clock_get_pts_prog(&au_uref_s, &date))) {
        upipe_dbg(upipe, "approximate a DTS from a PTS");
        uref_clock_set_dts_pts_delay(&au_uref_s,
                upipe_h264f->max_dec_frame_buffering * upipe_h264f->duration);
        uref_clock_set_dts_pts_delay(uref,
                upipe_h264f->max_dec_frame_buffering * upipe_h264f->duration);
    } else
        uref_clock_delete_dts_pts_delay(uref);

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(&au_uref_s, &date))) {          \
        uref_clock_set_dts_##dv(uref, date);                                \
        if (!ubase_check(uref_clock_get_dts_##dv(&upipe_h264f->au_uref_s,   \
                                                 NULL)))                    \
            uref_clock_set_dts_##dv(&upipe_h264f->au_uref_s,                \
                                    date + duration);                       \
    } else if (ubase_check(uref_clock_get_dts_##dv(uref, &date)))           \
        uref_clock_set_date_##dv(uref, UINT64_MAX, UREF_DATE_NONE);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    if (drift_rate.den)
        uref_clock_set_rate(uref, drift_rate);
    else
        uref_clock_delete_rate(uref);

    upipe_h264f->au_size = 0;
    upipe_h264f->au_vcl_offset = -1;
    upipe_h264f->au_slice = false;
    upipe_h264f->au_slice_nal = UINT8_MAX;
    upipe_h264f->pic_struct = -1;
    upipe_h264f->dpb_output_delay = UINT64_MAX;

    return uref;
}

/** @internal @This outputs the previous access unit, before the current NAL.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h264f_output_prev_annexb(struct upipe *upipe,
                                           struct upump **upump_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    size_t slice_size = upipe_h264f->au_size -
                        upipe_h264f->au_last_nal_offset;
    upipe_h264f->au_size = upipe_h264f->au_last_nal_offset;
    struct uref *uref = upipe_h264f_prepare_annexb(upipe);
    upipe_h264f->au_size = slice_size;
    upipe_h264f->au_last_nal_offset = 0;

    if (uref == NULL)
        return;

    if (unlikely(upipe_h264f->flow_def_requested == NULL)) {
        upipe_h264f->uref_output = uref;
        return;
    }

    upipe_h264f_output_au(upipe, uref, upump_p);
}

/** @internal @This is called when a new NAL starts, to check the previous NAL.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h264f_end_annexb(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (unlikely(!upipe_h264f->acquired)) {
        if (upipe_h264f->au_size) {
            /* we need to discard previous data */
            upipe_warn(upipe, "discarding non-sync data");
            upipe_h264f_consume_uref_stream(upipe, upipe_h264f->au_size);
            upipe_h264f->au_size = 0;
        }
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
        if (unlikely(upipe_h264f->got_discontinuity)) {
            upipe_warn(upipe, "outputting corrupt data");
            uref_flow_set_error(upipe_h264f->next_uref);
        }

        uint64_t nal_offset = upipe_h264f->au_last_nal_offset +
                              upipe_h264f->au_last_nal_start_size - 1;
        int err = upipe_h264f_handle_nal(upipe, upipe_h264f->next_uref->ubuf,
                                         nal_offset,
                                         upipe_h264f->au_size - nal_offset,
                                         &upipe_h264f->au_slice, NULL);
        if (err == UBASE_ERR_BUSY) {
            upipe_h264f_output_prev_annexb(upipe, upump_p);
            nal_offset = upipe_h264f->au_last_nal_offset +
                         upipe_h264f->au_last_nal_start_size - 1,
            err = upipe_h264f_handle_nal(upipe, upipe_h264f->next_uref->ubuf,
                                         nal_offset,
                                         upipe_h264f->au_size - nal_offset,
                                         &upipe_h264f->au_slice, NULL);
        }

        if (!ubase_check(err)) {
            upipe_warn(upipe, "discarding invalid slice data");
            upipe_h264f_consume_uref_stream(upipe, upipe_h264f->au_size);
            upipe_h264f->au_size = 0;
            return;
        }
        if (last_nal_type == H264NAL_TYPE_IDR) {
            uref_flow_set_random(upipe_h264f->next_uref);
        }
        return;
    }

    if (upipe_h264f->got_discontinuity) {
        /* discard the entire NAL */
        upipe_warn(upipe, "discarding non-slice data due to discontinuity");
        upipe_h264f_consume_uref_stream(upipe, upipe_h264f->au_size);
        upipe_h264f->au_size = 0;
        return;
    }

    uint64_t nal_offset = upipe_h264f->au_last_nal_offset +
                          upipe_h264f->au_last_nal_start_size - 1;
    int err = upipe_h264f_handle_nal(upipe, upipe_h264f->next_uref->ubuf,
                                     nal_offset,
                                     upipe_h264f->au_size - nal_offset,
                                     &upipe_h264f->au_slice, NULL);
    if (!ubase_check(err))
        upipe_warn_va(upipe, "passing invalid header (NAL %"PRIu8")",
                      last_nal_type);
}

/** @internal @This is called when a new NAL starts, to check it.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 * @param true if the NAL was completely handled
 */
static bool upipe_h264f_begin_annexb(struct upipe *upipe,
                                     struct upump **upump_p)
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
            if (!upipe_h264f->au_slice)
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
            if (!upipe_h264f->au_slice)
                return false;
            break;

        case H264NAL_TYPE_ENDSEQ:
        case H264NAL_TYPE_ENDSTR:
            return false;

        case H264NAL_TYPE_AUD:
        case H264NAL_TYPE_SPS:
        case H264NAL_TYPE_SPSX:
        case H264NAL_TYPE_SSPS:
        case H264NAL_TYPE_PPS:
            if (!upipe_h264f->au_slice)
                return false;
            break;

        default:
            if (nal_type < 14 || nal_type > 18 || !upipe_h264f->au_slice)
                return false;
            break;
    }

    upipe_h264f->au_size -= upipe_h264f->au_last_nal_start_size;
    struct uref *uref = upipe_h264f_prepare_annexb(upipe);
    upipe_h264f->au_size = upipe_h264f->au_last_nal_start_size;

    if (uref == NULL)
        return false;

    if (unlikely(upipe_h264f->flow_def_requested == NULL)) {
        upipe_h264f->uref_output = uref;
        return false;
    }

    upipe_h264f_output_au(upipe, uref, upump_p);
    return false;
}

/** @internal @This is called back by @ref upipe_h264f_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_promote_uref(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint64_t date, dts;
    if (ubase_check(uref_clock_get_dts_pts_delay(upipe_h264f->next_uref,
                                                 &date)))
        uref_clock_set_dts_pts_delay(&upipe_h264f->au_uref_s, date);
    else if (ubase_check(uref_clock_get_pts_prog(upipe_h264f->next_uref,
                                                 &date)) &&
             ubase_check(uref_clock_get_dts_prog(&upipe_h264f->au_uref_s,
                                                 &dts))) {
        if (date < dts) {
            upipe_dbg(upipe, "fixing up DTS overflow");
            date = dts;
        } else if (upipe_h264f->max_dec_frame_buffering != UINT32_MAX &&
                   date > dts + upipe_h264f->max_dec_frame_buffering *
                                upipe_h264f->duration * 2) {
            upipe_dbg(upipe, "fixing up DTS underflow");
            date = dts + upipe_h264f->max_dec_frame_buffering *
                         upipe_h264f->duration * 2;
        }
        uref_clock_set_dts_pts_delay(&upipe_h264f->au_uref_s, date - dts);
        uref_clock_set_dts_pts_delay(upipe_h264f->next_uref, date - dts);
    }

#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(upipe_h264f->next_uref, &dts))) \
        uref_clock_set_dts_##dv(&upipe_h264f->au_uref_s, dts);              \
    else if (ubase_check(uref_clock_get_pts_##dv(upipe_h264f->next_uref,    \
                                                 &date)))                   \
        uref_clock_set_pts_##dv(&upipe_h264f->au_uref_s, date);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_get_rate(upipe_h264f->next_uref, &upipe_h264f->drift_rate);
    if (ubase_check(uref_clock_get_dts_prog(upipe_h264f->next_uref, &date)))
        uref_clock_get_rap_sys(upipe_h264f->next_uref, &upipe_h264f->dts_rap);
}

/** @internal @This finds an H.264 annex B start code and returns its type.
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
    while (ubase_check(uref_block_read(upipe_h264f->next_uref,
                    upipe_h264f->au_size, &size, &buffer))) {
        const uint8_t *p = upipe_framers_mpeg_scan(buffer, buffer + size,
                                                   &upipe_h264f->scan_context);
        if (p > buffer + 5)
            *prev_p = p[-5];
        uref_block_unmap(upipe_h264f->next_uref, upipe_h264f->au_size);

        if ((upipe_h264f->scan_context & 0xffffff00) == 0x100) {
            *start_p = upipe_h264f->scan_context & 0xff;
            upipe_h264f->au_size += p - buffer;
            if (p <= buffer + 5 &&
                !ubase_check(uref_block_extract(upipe_h264f->next_uref,
                                    upipe_h264f->au_size - 5, 1, prev_p)))
                *prev_p = 0xff;
            return true;
        }
        upipe_h264f->au_size += size;
        size = -1;
    }
    return false;
}

/** @internal @This tries to output access units from the queue of input
 * buffers.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h264f_work_annexb(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    while (upipe_h264f->next_uref != NULL) {
        if (upipe_h264f->flow_def_requested == NULL &&
            upipe_h264f->flow_def_attr != NULL)
            return;

        uint8_t start, prev;
        if (!upipe_h264f_find(upipe, &start, &prev))
            break;
        size_t start_size = !prev ? 5 : 4;

        upipe_h264f->au_size -= start_size;
        upipe_h264f_end_annexb(upipe, upump_p);

        if (upipe_h264f->flow_def_requested == NULL &&
            upipe_h264f->flow_def_attr != NULL)
            return;

        if (upipe_h264f->au_size)
            uref_h26x_set_nal_offset(upipe_h264f->next_uref,
                                     upipe_h264f->au_size,
                                     upipe_h264f->au_nal_units++);
        upipe_h264f->au_size += start_size;
        upipe_h264f->got_discontinuity = false;
        upipe_h264f->au_last_nal = start;
        upipe_h264f->au_last_nal_start_size = start_size;
        if (upipe_h264f_begin_annexb(upipe, upump_p))
            upipe_h264f->au_last_nal_offset = -1;
        else
            upipe_h264f->au_last_nal_offset = upipe_h264f->au_size - start_size;
    }

    if (!upipe_h264f->complete_input || !upipe_h264f->au_size)
       return;

    upipe_h264f_end_annexb(upipe, upump_p);
    struct uref *uref = upipe_h264f_prepare_annexb(upipe);

    if (uref == NULL)
        return;

    if (unlikely(upipe_h264f->flow_def_requested == NULL)) {
        upipe_h264f->uref_output = uref;
        return;
    }

    upipe_h264f_output_au(upipe, uref, upump_p);
}

/** @internal @This prepares a raw access unit.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure containing one frame
 */
static void upipe_h264f_prepare_raw(struct upipe *upipe, struct uref *uref)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    int err = upipe_h264f_prepare_au(upipe, uref);
    UBASE_FATAL(upipe, err);

    uint64_t duration = 0;
    uref_clock_get_duration(uref, &duration);

    uint64_t date, pts;
    if (upipe_h264f->dpb_output_delay != UINT64_MAX)
        uref_clock_set_dts_pts_delay(uref,
                upipe_h264f->dpb_output_delay * upipe_h264f->duration * 2);
    else if (!upipe_h264f->max_dec_frame_buffering)
        uref_clock_set_dts_pts_delay(uref, 0);
    else if (!ubase_check(uref_clock_get_dts_pts_delay(uref, &date)) &&
             ubase_check(uref_clock_get_pts_prog(uref, &pts))) {
        if (ubase_check(uref_clock_get_dts_prog(&upipe_h264f->au_uref_s,
                                                &date))) {
            if (pts < date) {
                upipe_dbg(upipe, "fixing up DTS overflow");
                pts = date;
            } else if (upipe_h264f->max_dec_frame_buffering != UINT32_MAX &&
                       pts > date + upipe_h264f->max_dec_frame_buffering *
                                    upipe_h264f->duration * 2) {
                upipe_dbg(upipe, "fixing up DTS underflow");
                pts = date + upipe_h264f->max_dec_frame_buffering *
                             upipe_h264f->duration * 2;
            }
            uref_clock_set_dts_pts_delay(&upipe_h264f->au_uref_s, pts - date);
            uref_clock_set_dts_pts_delay(uref, pts - date);

        } else if (upipe_h264f->max_dec_frame_buffering != UINT32_MAX) {
            upipe_dbg(upipe, "approximate a DTS from a PTS");
            uref_clock_set_dts_pts_delay(uref,
                                         upipe_h264f->max_dec_frame_buffering *
                                         upipe_h264f->duration * 2);
        }
    }

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(uref, &date))) {                \
        uref_clock_rebase_dts_##dv(uref);                                   \
        uref_clock_set_dts_##dv(&upipe_h264f->au_uref_s, date + duration);  \
    } else if (ubase_check(uref_clock_get_dts_##dv(&upipe_h264f->au_uref_s, \
                                                   &date))) {               \
        uref_clock_set_dts_##dv(uref, date);                                \
        uref_clock_set_dts_##dv(&upipe_h264f->au_uref_s, date + duration);  \
    }
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    uref_clock_delete_dts_pts_delay(&upipe_h264f->au_uref_s);
    upipe_h264f->pic_struct = -1;
    upipe_h264f->dpb_output_delay = UINT64_MAX;
}

/** @internal @This works on incoming frames in NALU format (supposedly
 * one frame per uref guaranteed by demux).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure containing one frame
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_h264f_work_nalu(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    uint64_t nal_units = 0;
    uint64_t nal_offset = 0;
    uint64_t nal_size = 0;
    bool au_slice = false;
    ssize_t vcl_offset = -1;
    while (ubase_check(uref_h26x_iterate_nal(uref, &nal_units,
                                             &nal_offset, &nal_size, 0))) {
        uint8_t nal;

        int err = upipe_h264f_handle_nal(upipe, uref->ubuf, nal_offset,
                                         nal_size, &au_slice, &nal);
        if (!ubase_check(err)) {
            upipe_warn(upipe, "invalid NAL received");
            upipe_throw_error(upipe, err);
            uref_free(uref);
            return true;
        }
        uint8_t nal_type = h264nalst_get_type(nal);
        if (nal_type == H264NAL_TYPE_IDR)
            uref_flow_set_random(uref);
        if (h264naltype_is_vcl(nal_type) && vcl_offset == -1)
            vcl_offset = nal_offset;
    }

    UBASE_RETURN(uref_block_set_header_size(uref, vcl_offset))

    upipe_h264f_prepare_raw(upipe, uref);
    upipe_h264f_output_au(upipe, uref, upump_p);
    return true;
}

/** @internal @This works on incoming frames from length-based formats
 * (supposedly one frame per uref guaranteed by demux).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure containing one frame
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_h264f_work_length(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    uint64_t nal_offset = 0;
    size_t uref_size;
    if (unlikely(!ubase_check(uref_block_size(uref, &uref_size)))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return true;
    }

    uint8_t length_size;
    switch (upipe_h264f->encaps_input) {
        case UREF_H26X_ENCAPS_LENGTH1: length_size = 1; break;
        case UREF_H26X_ENCAPS_LENGTH2: length_size = 2; break;
        default:
        case UREF_H26X_ENCAPS_LENGTH4: length_size = 4; break;
    }

    uint64_t au_nal_units = 0;
    bool au_slice = false;
    ssize_t vcl_offset = -1;
    while (nal_offset + length_size <= uref_size) {
        uint8_t length[length_size];
        if (unlikely(!ubase_check(uref_block_extract(uref, nal_offset,
                                                     length_size, length)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return true;
        }

        uint32_t nal_size = 0;
        for (int i = 0; i < length_size; i++) {
            nal_size <<= 8;
            nal_size |= length[i];
        }

        if (nal_offset + length_size + nal_size > uref_size) {
            upipe_warn_va(upipe, "NALU larger than buffer (%"PRIu64" > %zu)",
                          nal_offset + length_size + nal_size, uref_size);
            uref_free(uref);
            return true;
        }

        uint8_t nal;
        int err = upipe_h264f_handle_nal(upipe, uref->ubuf,
                                         nal_offset + length_size,
                                         nal_size, &au_slice, &nal);
        if (!ubase_check(err)) {
            upipe_warn(upipe, "invalid NAL received");
            upipe_throw_error(upipe, err);
        }
        uint8_t nal_type = h264nalst_get_type(nal);
        if (nal_type == H264NAL_TYPE_IDR)
            uref_flow_set_random(uref);
        if (h264naltype_is_vcl(nal_type) && vcl_offset == -1)
            vcl_offset = nal_offset;

        if (nal_offset)
            uref_h26x_set_nal_offset(uref, nal_offset, au_nal_units++);
        nal_offset += length_size + nal_size;
    }

    UBASE_RETURN(uref_block_set_header_size(uref, vcl_offset))

    upipe_h264f_prepare_raw(upipe, uref);
    upipe_h264f_output_au(upipe, uref, upump_p);
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_h264f_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_h264f->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_h264f->input_latency);
        upipe_h264f->encaps_input = uref_h26x_flow_infer_encaps(uref);
        upipe_h264f->complete_input = ubase_check(uref_flow_get_complete(uref));
        upipe_h264f_store_flow_def(upipe, NULL);
        uref_free(upipe_h264f->flow_def_requested);
        upipe_h264f->flow_def_requested = NULL;
        uref = upipe_h264f_store_flow_def_input(upipe, uref);
        if (uref != NULL)
            upipe_h264f_require_flow_format(upipe, uref);
        upipe_h264f_handle_global(upipe);
        return true;
    }

    if (upipe_h264f->flow_def_requested == NULL &&
        upipe_h264f->flow_def_attr != NULL)
        return false;

    if (upipe_h264f->flow_def_input == NULL) {
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return true;
    }

    const uint8_t *p;
    size_t size;
    if (unlikely(ubase_check(uref_flow_get_headers(uref, &p, &size)))) {
        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_h264f->ubuf_mgr,
                                                         p, size);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return true;
        }
        uref_flow_delete_headers(uref);
        uref_attach_ubuf(uref, ubuf);
    }

    switch (upipe_h264f->encaps_input) {
        case UREF_H26X_ENCAPS_NALU:
            return upipe_h264f_work_nalu(upipe, uref, upump_p);
        case UREF_H26X_ENCAPS_LENGTH1:
        case UREF_H26X_ENCAPS_LENGTH2:
        case UREF_H26X_ENCAPS_LENGTH4:
            return upipe_h264f_work_length(upipe, uref, upump_p);
        case UREF_H26X_ENCAPS_ANNEXB:
            break;
        default:
            upipe_err(upipe, "global headers not found");
            uref_free(uref);
            return true;
    }

    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref))))
        upipe_h264f->got_discontinuity = true;

    upipe_h264f_append_uref_stream(upipe, uref);
    upipe_h264f_work_annexb(upipe, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h264f_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    if (!upipe_h264f_check_input(upipe)) {
        upipe_h264f_hold_input(upipe, uref);
        upipe_h264f_block_input(upipe, upump_p);
    } else if (!upipe_h264f_handle(upipe, uref, upump_p)) {
        upipe_h264f_hold_input(upipe, uref);
        upipe_h264f_block_input(upipe, upump_p);
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
static int upipe_h264f_check_flow_format(struct upipe *upipe,
                                         struct uref *flow_format)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    upipe_h264f_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This receives the result of a ubuf manager request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_h264f_check_ubuf_mgr(struct upipe *upipe,
                                      struct uref *flow_format)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;
    if (upipe_h264f->flow_def_attr == NULL) {
        /* temporary ubuf manager, will be overwritten later */
        uref_free(flow_format);
        return UBASE_ERR_NONE;
    }

    uref_free(upipe_h264f->flow_def_requested);
    upipe_h264f->flow_def_requested = flow_format;
    upipe_h264f->encaps_output = uref_h26x_flow_infer_encaps(flow_format);
    ubuf_free(upipe_h264f->annexb_header);
    upipe_h264f->annexb_header =
        upipe_h26xf_alloc_annexb(upipe_h264f->ubuf_mgr);
    UBASE_ALLOC_RETURN(upipe_h264f->annexb_header);

    ubuf_free(upipe_h264f->annexb_aud);
    uint8_t aud_buffer[5] = { 0, 0, 0, 1, H264NAL_TYPE_AUD };
    upipe_h264f->annexb_aud =
        ubuf_block_alloc_from_opaque(upipe_h264f->ubuf_mgr, aud_buffer, 5);
    UBASE_ALLOC_RETURN(upipe_h264f->annexb_aud);

    upipe_h264f_build_flow_def(upipe);

    if (upipe_h264f->uref_output) {
        upipe_h264f_output_au(upipe, upipe_h264f->uref_output, NULL);
        upipe_h264f->uref_output = NULL;
    }

    bool was_buffered = !upipe_h264f_check_input(upipe);
    upipe_h264f_output_input(upipe);
    upipe_h264f_unblock_input(upipe);
    if (was_buffered && upipe_h264f_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_h264f_input. */
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
static int upipe_h264f_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.h264.") && strcmp(def, "block."))))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (upipe_h264f->ubuf_mgr == NULL) {
        /* We have to get a ubuf manager to parse the global headers. */
        upipe_h264f_demand_ubuf_mgr(upipe, flow_def_dup);

        if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    }
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a h264f pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_h264f_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_h264f_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_h264f_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h264f_free(struct upipe *upipe)
{
    struct upipe_h264f *upipe_h264f = upipe_h264f_from_upipe(upipe);

    /* Output any buffered frame. */
    if (upipe_h264f->encaps_input == UREF_H26X_ENCAPS_ANNEXB &&
        !upipe_h264f->complete_input && upipe_h264f->au_size) {
        upipe_h264f_end_annexb(upipe, NULL);
        struct uref *uref = upipe_h264f_prepare_annexb(upipe);
        if (uref != NULL)
            upipe_h264f_output_au(upipe, uref, NULL);
    }

    upipe_throw_dead(upipe);

    upipe_h264f_clean_uref_stream(upipe);
    upipe_h264f_clean_input(upipe);
    upipe_h264f_clean_output(upipe);
    uref_free(upipe_h264f->flow_def_requested);
    uref_free(upipe_h264f->uref_output);
    ubuf_free(upipe_h264f->annexb_header);
    ubuf_free(upipe_h264f->annexb_aud);
    upipe_h264f_clean_flow_format(upipe);
    upipe_h264f_clean_flow_def(upipe);
    upipe_h264f_clean_ubuf_mgr(upipe);
    upipe_h264f_clean_sync(upipe);

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

    upipe_h264f_clean_urefcount(upipe);
    upipe_h264f_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_h264f_mgr = {
    .refcount = NULL,
    .signature = UPIPE_H264F_SIGNATURE,

    .upipe_alloc = upipe_h264f_alloc,
    .upipe_input = upipe_h264f_input,
    .upipe_control = upipe_h264f_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all h264f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_h264f_mgr_alloc(void)
{
    return &upipe_h264f_mgr;
}

/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of an ITU-T H.265 stream
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
#include <upipe-framers/upipe_h265_framer.h>
#include <upipe-framers/uref_h265.h>
#include <upipe-framers/uref_h26x.h>
#include <upipe-framers/uref_h265_flow.h>
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

#include <bitstream/itu/h265.h>

/** @internal @This is the private context of an h265f pipe. */
struct upipe_h265f {
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
    /** rap of the last vps */
    uint64_t vps_rap;
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
    /** pointers to video parameter sets */
    struct ubuf *vps[H265VPS_ID_MAX];
    /** active video parameter set, or -1 */
    int active_vps;
    /** pointers to sequence parameter sets */
    struct ubuf *sps[H265SPS_ID_MAX];
    /** active sequence parameter set, or -1 */
    int active_sps;
    /** pointers to picture parameter sets */
    struct ubuf *pps[H265PPS_ID_MAX];
    /** active picture parameter set, or -1 */
    int active_pps;

    /* parsing results - headers */
    /** VPS profile space */
    uint8_t profile_space;
    /** VPS tier */
    bool tier;
    /** VPS profile index */
    uint8_t profile_idc;
    /** VPS profile compatibility */
    uint8_t profile_compatibility;
    /** VPS level index */
    uint8_t level_idc;
    /** VPS constraint indicator */
    uint64_t constraint_indicator;
    /** VPS general progressive flag */
    bool general_progressive;
    /** VPS general interlaced flag */
    bool general_interlaced;
    /** VPS time scale */
    uint32_t time_scale;
    /** VPS frame rate */
    struct urational frame_rate;
    /** VPS octet rate */
    uint64_t octet_rate;
    /** VPS CPB size */
    uint64_t cpb_size;
    /** SPS chroma format idc */
    uint32_t chroma_idc;
    /** duration of a frame */
    uint64_t duration;
    /** true if frame_field is present */
    bool frame_field_present;
    /** number of extra slice header bits from PPS */
    uint8_t num_extra_slice_header_bits;

    /* parsing results - slice */
    /** picture structure */
    int pic_struct;
    /** slice type */
    uint32_t slice_type;

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
static void upipe_h265f_promote_uref(struct upipe *upipe);
/** @hidden */
static bool upipe_h265f_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p);
/** @hidden */
static int upipe_h265f_check_flow_format(struct upipe *upipe,
                                         struct uref *flow_format);
/** @hidden */
static int upipe_h265f_check_ubuf_mgr(struct upipe *upipe,
                                      struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_h265f, upipe, UPIPE_H265F_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_h265f, urefcount, upipe_h265f_free)
UPIPE_HELPER_VOID(upipe_h265f)
UPIPE_HELPER_SYNC(upipe_h265f, acquired)
UPIPE_HELPER_UREF_STREAM(upipe_h265f, next_uref, next_uref_size, urefs,
                         upipe_h265f_promote_uref)

UPIPE_HELPER_OUTPUT(upipe_h265f, output, flow_def, output_state, request_list)
UPIPE_HELPER_INPUT(upipe_h265f, request_urefs, nb_urefs, max_urefs, blockers, upipe_h265f_handle)
UPIPE_HELPER_FLOW_FORMAT(upipe_h265f, request, upipe_h265f_check_flow_format,
                         upipe_h265f_register_output_request,
                         upipe_h265f_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_h265f, flow_def_input, flow_def_attr)
UPIPE_HELPER_UBUF_MGR(upipe_h265f, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_h265f_check_ubuf_mgr,
                      upipe_h265f_register_output_request,
                      upipe_h265f_unregister_output_request)

/** @internal @This flushes all dates.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h265f_flush_dates(struct upipe *upipe)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    uref_clock_set_date_sys(&upipe_h265f->au_uref_s, UINT64_MAX,
                            UREF_DATE_NONE);
    uref_clock_set_date_prog(&upipe_h265f->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_set_date_orig(&upipe_h265f->au_uref_s, UINT64_MAX,
                             UREF_DATE_NONE);
    uref_clock_delete_dts_pts_delay(&upipe_h265f->au_uref_s);
    upipe_h265f->drift_rate.num = upipe_h265f->drift_rate.den = 0;
}

/** @internal @This allocates an h265f pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_h265f_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_h265f_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    upipe_h265f_init_urefcount(upipe);
    upipe_h265f_init_sync(upipe);
    upipe_h265f_init_uref_stream(upipe);
    upipe_h265f_init_output(upipe);
    upipe_h265f_init_input(upipe);
    upipe_h265f_init_flow_format(upipe);
    upipe_h265f_init_flow_def(upipe);
    upipe_h265f_init_ubuf_mgr(upipe);
    upipe_h265f->flow_def_requested = NULL;
    upipe_h265f->encaps_input = upipe_h265f->encaps_output =
        UREF_H26X_ENCAPS_ANNEXB;
    upipe_h265f->complete_input = false;
    upipe_h265f->uref_output = NULL;
    upipe_h265f->annexb_header = NULL;
    upipe_h265f->annexb_aud = NULL;
    upipe_h265f->dts_rap = UINT64_MAX;
    upipe_h265f->vps_rap = UINT64_MAX;
    upipe_h265f->sps_rap = UINT64_MAX;
    upipe_h265f->pps_rap = UINT64_MAX;
    upipe_h265f->iframe_rap = UINT64_MAX;
    upipe_h265f->input_latency = 0;
    upipe_h265f->profile_space = 0;
    upipe_h265f->tier = false;
    upipe_h265f->profile_idc = 0;
    upipe_h265f->profile_compatibility = 0;
    upipe_h265f->level_idc = 0;
    upipe_h265f->general_progressive = 0;
    upipe_h265f->general_interlaced = 0;
    upipe_h265f->constraint_indicator = 0;
    upipe_h265f->pic_struct = -1;
    upipe_h265f->duration = 0;
    upipe_h265f->got_discontinuity = false;
    upipe_h265f->scan_context = UINT32_MAX;
    upipe_h265f->au_size = 0;
    upipe_h265f->au_last_nal_offset = -1;
    upipe_h265f->au_last_nal = UINT8_MAX;
    upipe_h265f->au_last_nal_start_size = 0;
    upipe_h265f->au_nal_units = 0;
    upipe_h265f->au_vcl_offset = -1;
    upipe_h265f->au_slice = false;
    uref_init(&upipe_h265f->au_uref_s);
    upipe_h265f_flush_dates(upipe);

    int i;
    for (i = 0; i < H265VPS_ID_MAX; i++)
        upipe_h265f->vps[i] = NULL;
    upipe_h265f->active_vps = -1;

    for (i = 0; i < H265SPS_ID_MAX; i++)
        upipe_h265f->sps[i] = NULL;
    upipe_h265f->active_sps = -1;

    for (i = 0; i < H265PPS_ID_MAX; i++)
        upipe_h265f->pps[i] = NULL;
    upipe_h265f->active_pps = -1;

    upipe_h265f->acquired = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This parses profile tier level structure.
 *
 * @param upipe description structure of the pipe
 * @param s ubuf block stream
 * @param max_subl_1 maxNumSubLayersMinus1 structure
 * @param profile_space_p filled in with the profile space
 * @param tier_p filled in with the tier flag
 * @param profile_idc_p filled in with the profile index
 * @param profile_compatibility_p filled in with the profile compatibility
 * @param level_idc_p filled in with the level index
 * @param general_progressive_p filled in with the general progressive flag
 * @param general_interlaced_p filled in with the general interlaced flag
 * @param constraint_indicator_p filled in with the constraint indicator field
 */
static void upipe_h265f_stream_parse_ptl(struct upipe *upipe,
                                         struct ubuf_block_stream *s,
                                         uint8_t max_subl_1,
                                         uint8_t *profile_space_p,
                                         bool *tier_p,
                                         uint8_t *profile_idc_p,
                                         uint32_t *profile_compatibility_p,
                                         uint8_t *level_idc_p,
                                         bool *general_progressive_p,
                                         bool *general_interlaced_p,
                                         uint64_t *constraint_indicator_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    upipe_h26xf_stream_fill_bits(s, 8);
    uint8_t profile_space = ubuf_block_stream_show_bits(s, 2);
    ubuf_block_stream_skip_bits(s, 2);
    if (profile_space_p != NULL)
        *profile_space_p = profile_space;

    bool tier = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (tier_p != NULL)
        *tier_p = tier;

    uint8_t profile_idc = ubuf_block_stream_show_bits(s, 5);
    ubuf_block_stream_skip_bits(s, 5);
    if (profile_idc_p != NULL)
        *profile_idc_p = profile_idc;

    upipe_h26xf_stream_fill_bits(s, 16);
    uint64_t profile_compatibility =
        (uint64_t)ubuf_block_stream_show_bits(s, 16) << 16;
    ubuf_block_stream_skip_bits(s, 16);
    upipe_h26xf_stream_fill_bits(s, 16);
    profile_compatibility |=
        (uint64_t)ubuf_block_stream_show_bits(s, 16);
    ubuf_block_stream_skip_bits(s, 16);
    if (profile_compatibility_p != NULL)
        *profile_compatibility_p = profile_compatibility;

    upipe_h26xf_stream_fill_bits(s, 16);
    uint64_t constraint_indicator =
        (uint64_t)ubuf_block_stream_show_bits(s, 16) << 32;
    bool general_progressive = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    bool general_interlaced = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 15);
    if (general_progressive_p != NULL)
        *general_progressive_p = general_progressive;
    if (general_interlaced_p != NULL)
        *general_interlaced_p = general_interlaced;
    upipe_h26xf_stream_fill_bits(s, 16);
    constraint_indicator |=
        (uint64_t)ubuf_block_stream_show_bits(s, 16) << 16;
    ubuf_block_stream_skip_bits(s, 16);
    upipe_h26xf_stream_fill_bits(s, 16);
    constraint_indicator |=
        (uint64_t)ubuf_block_stream_show_bits(s, 16);
    ubuf_block_stream_skip_bits(s, 16);
    if (constraint_indicator_p != NULL)
        *constraint_indicator_p = constraint_indicator;

    upipe_h26xf_stream_fill_bits(s, 8);
    uint8_t level_idc = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    if (level_idc_p != NULL)
        *level_idc_p = level_idc;

    /* sublayers */
    bool subl_profile_present[max_subl_1];
    bool subl_level_present[max_subl_1];
    for (int i = 0; i < max_subl_1; i++) {
        upipe_h26xf_stream_fill_bits(s, 2);
        subl_profile_present[i] = ubuf_block_stream_show_bits(s, 1);
        subl_level_present[i] = ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 2);
    }
    if (max_subl_1) {
        for (int i = max_subl_1; i < 8; i++) {
            upipe_h26xf_stream_fill_bits(s, 2);
            ubuf_block_stream_skip_bits(s, 2);
        }
    }

    for (int i = 0; i < max_subl_1; i++) {
        if (subl_profile_present[i]) {
            for (int i = 0; i < H265PTL_PROFILE_SIZE; i++) {
                upipe_h26xf_stream_fill_bits(s, 8);
                ubuf_block_stream_skip_bits(s, 8);
            }
        }
        if (subl_level_present[i]) {
            upipe_h26xf_stream_fill_bits(s, 8);
            ubuf_block_stream_skip_bits(s, 8);
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
static int upipe_h265f_stream_parse_hrd(struct upipe *upipe,
                                        struct ubuf_block_stream *s,
                                        uint64_t *octetrate_p,
                                        uint64_t *cpb_size_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    upipe_h26xf_stream_fill_bits(s, 2);
    bool nal_hrd_present = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    bool vcl_hrd_present = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);

    uint8_t bitrate_scale = 0, cpb_size_scale = 0;
    if (nal_hrd_present || vcl_hrd_present) {
        upipe_h26xf_stream_fill_bits(s, 8);
        bool sub_pic_hrd = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (sub_pic_hrd) {
            upipe_h26xf_stream_fill_bits(s, 19);
            ubuf_block_stream_skip_bits(s, 19);
        }

        upipe_h26xf_stream_fill_bits(s, 8);
        bitrate_scale = ubuf_block_stream_show_bits(s, 4);
        ubuf_block_stream_skip_bits(s, 4);
        cpb_size_scale = ubuf_block_stream_show_bits(s, 4);
        ubuf_block_stream_skip_bits(s, 4);
        if (sub_pic_hrd) {
            upipe_h26xf_stream_fill_bits(s, 4);
            ubuf_block_stream_skip_bits(s, 4); /* cpb_size_du_scale */
        }
        /* initial_cpb_removal_delay_length_minus1,
         * au_cpb_removal_delay_length_minus1,
         * dpb_output_delay_length_minus1 */
        upipe_h26xf_stream_fill_bits(s, 15);
        ubuf_block_stream_skip_bits(s, 15);
    }

    upipe_h26xf_stream_fill_bits(s, 8);
    bool fixed_pic_rate = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    bool fixed_pic_rate_within_cvs = true;
    if (!fixed_pic_rate) {
        fixed_pic_rate_within_cvs = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
    } 
    bool low_delay = false;
    if (fixed_pic_rate_within_cvs)
        upipe_h26xf_stream_ue(s); /* elemental_duration_in_tc_minus1 */
    else {
        upipe_h26xf_stream_fill_bits(s, 1);
        low_delay = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
    }
    if (!low_delay)
        upipe_h26xf_stream_ue(s); /* cpb_cnt_minus1 */

    /* Use first value to deduce bitrate and cpb size */
    *octetrate_p =
        (((uint64_t)upipe_h26xf_stream_ue(s) + 1) << (6 + bitrate_scale)) / 8;
    *cpb_size_p =
        (((uint64_t)upipe_h26xf_stream_ue(s) + 1) << (4 + cpb_size_scale)) / 8;

    /* incomplete parsing */
    return UBASE_ERR_NONE;
}

/** @internal @This parses scaling lists.
 *
 * @param s ubuf block stream
 */
static void upipe_h265f_stream_parse_scaling(struct ubuf_block_stream *s)
{
    for (int size_id = 0; size_id < 4; size_id++) {
        for (int matrix_id = 0; matrix_id < (size_id == 3 ? 2 : 6);
             matrix_id++) {
            upipe_h26xf_stream_fill_bits(s, 1);
            bool pred_mode = ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            if (!pred_mode)
                upipe_h26xf_stream_ue(s); /* pred_matrix_id_delta */
            else {
                if (size_id > 1)
                    upipe_h26xf_stream_se(s); /* dc_coef */

                int coef_num = 1 << (4 + (size_id << 1));
                if (coef_num > 64)
                    coef_num = 64;
                for (int i = 0; i < coef_num; i++)
                    upipe_h26xf_stream_se(s); /* delta_coef */
            }
        }
    }
}

/** @internal @This parses a short-term ref pic set.
 *
 * @param s ubuf block stream
 * @param idx set index
 * @param max max short term ref pic sets
 * @param max_dec_pic_buffering_1 sps_max_dec_pic_buffering_minux1[sps_max_sub_layers_minus1]
 * @param num_delta_pocs array of num_delta_pocs
 * @return false in case of error
 */
static bool
upipe_h265f_stream_parse_short_term_ref_pic_set(struct ubuf_block_stream *s,
        int idx, uint32_t max, uint32_t max_dec_pic_buffering_1,
        uint32_t num_delta_pocs[])
{
    bool prediction_flag = false;
    if (idx) {
        upipe_h26xf_stream_fill_bits(s, 1);
        prediction_flag = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
    }

    if (prediction_flag) {
        uint32_t delta_idx = 1;
        if (idx == max)
            delta_idx = upipe_h26xf_stream_ue(s) + 1;
        upipe_h26xf_stream_fill_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        upipe_h26xf_stream_ue(s);
        int ref_idx = delta_idx > idx ? 0 : delta_idx;
        for (int i = 0; i < num_delta_pocs[ref_idx]; i++) {
            upipe_h26xf_stream_fill_bits(s, 2);
            bool used_by_curr_pic = !!ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            if (used_by_curr_pic)
                ubuf_block_stream_skip_bits(s, 1);
        }
    } else {
        uint32_t num_negative_pics = upipe_h26xf_stream_ue(s);
        if (num_negative_pics > max_dec_pic_buffering_1)
            return false;
        uint32_t num_positive_pics = upipe_h26xf_stream_ue(s);
        if (num_positive_pics > max_dec_pic_buffering_1 - num_negative_pics)
            return false;
        num_delta_pocs[idx] = num_negative_pics + num_positive_pics;
        for (int i = 0; i < num_negative_pics; i++) {
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_fill_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }
        for (int i = 0; i < num_positive_pics; i++) {
            upipe_h26xf_stream_ue(s);
            upipe_h26xf_stream_fill_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
        }
    }
    return true;
}

/** @internal @This activates a video parameter set.
 *
 * @param upipe description structure of the pipe
 * @param vps_id VPS to activate
 * @return false if the VPS couldn't be activated
 */
static bool upipe_h265f_activate_vps(struct upipe *upipe, uint32_t vps_id)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (likely(upipe_h265f->active_vps == vps_id))
        return true;
    if (unlikely(upipe_h265f->vps[vps_id] == NULL)) {
        upipe_err(upipe, "invalid VPS ID");
        return false;
    }

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, upipe_h265f->vps[vps_id], 2))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    upipe_h26xf_stream_fill_bits(s, 16);
    ubuf_block_stream_skip_bits(s, 12);
    uint8_t max_subl_1 = ubuf_block_stream_show_bits(s, 3);
    ubuf_block_stream_skip_bits(s, 4);
    upipe_h26xf_stream_fill_bits(s, 16);
    ubuf_block_stream_skip_bits(s, 16);

    bool tier, general_progressive, general_interlaced;
    uint8_t profile_space, profile_idc, level_idc;
    uint32_t profile_compatibility;
    uint64_t constraint_indicator;
    upipe_h265f_stream_parse_ptl(upipe, s, max_subl_1, &profile_space, &tier,
                                 &profile_idc, &profile_compatibility,
                                 &level_idc,
                                 &general_progressive, &general_interlaced,
                                 &constraint_indicator);
    upipe_h265f->profile_space = profile_space;
    upipe_h265f->tier = tier;
    upipe_h265f->profile_idc = profile_idc;
    upipe_h265f->profile_compatibility = profile_compatibility;
    upipe_h265f->level_idc = level_idc;
    upipe_h265f->general_progressive = general_progressive;
    upipe_h265f->general_interlaced = general_interlaced;
    upipe_h265f->constraint_indicator = constraint_indicator;

    upipe_h265f->active_vps = vps_id;
    ubuf_block_stream_clean(s);
    return true;
}

/** @internal @This activates a sequence parameter set.
 *
 * @param upipe description structure of the pipe
 * @param sps_id SPS to activate
 * @return false if the SPS couldn't be activated
 */
static bool upipe_h265f_activate_sps(struct upipe *upipe, uint32_t sps_id)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (likely(upipe_h265f->active_sps == sps_id))
        return true;
    if (unlikely(upipe_h265f->sps[sps_id] == NULL)) {
        upipe_err(upipe, "invalid SPS ID");
        return false;
    }

    struct uref *flow_def = upipe_h265f_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    UBASE_FATAL(upipe, uref_flow_set_complete(flow_def))
    UBASE_FATAL(upipe, uref_h26x_flow_set_encaps(flow_def,
                upipe_h265f->encaps_input))

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, upipe_h265f->sps[sps_id], 2))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    upipe_h26xf_stream_fill_bits(s, 8);
    uint8_t vps_id = ubuf_block_stream_show_bits(s, 4);
    ubuf_block_stream_skip_bits(s, 4);
    uint8_t max_subl_1 = ubuf_block_stream_show_bits(s, 3);
    ubuf_block_stream_skip_bits(s, 4);

    if (!upipe_h265f_activate_vps(upipe, vps_id)) {
        ubuf_block_stream_clean(s);
        return false;
    }

    /* Import attributes from the VPS. */
    bool tier = upipe_h265f->tier;
    if (tier) {
        UBASE_FATAL(upipe, uref_h265_flow_set_tier(flow_def))
    }
    UBASE_FATAL(upipe,
            uref_h265_flow_set_profile(flow_def, upipe_h265f->profile_idc))
    UBASE_FATAL(upipe,
            uref_h265_flow_set_level(flow_def, upipe_h265f->level_idc))

    uint64_t max_octetrate, max_bs;
    switch (upipe_h265f->level_idc) {
        case H265VPS_LEVEL_1_0:
            max_octetrate = 128000 / 8;
            max_bs = 350000 / 8;
            break;
        case H265VPS_LEVEL_2_0:
            max_octetrate = max_bs = 1500000 / 8;
            break;
        case H265VPS_LEVEL_2_1:
            max_octetrate = max_bs = 3000000 / 8;
            break;
        case H265VPS_LEVEL_3_0:
            max_octetrate = max_bs = 6000000 / 8;
            break;
        case H265VPS_LEVEL_3_1:
            max_octetrate = max_bs = 10000000 / 8;
            break;
        case H265VPS_LEVEL_4_0:
            max_octetrate = max_bs = tier ? (30000000 / 8) : (12000000 / 8);
            break;
        case H265VPS_LEVEL_4_1:
            max_octetrate = max_bs = tier ? (50000000 / 8) : (20000000 / 8);
            break;
        case H265VPS_LEVEL_5_0:
            max_octetrate = max_bs = tier ? (100000000 / 8) : (25000000 / 8);
            break;
        case H265VPS_LEVEL_5_1:
            max_octetrate = max_bs = tier ? (160000000 / 8) : (40000000 / 8);
            break;
        case H265VPS_LEVEL_5_2:
            max_octetrate = max_bs = tier ? (240000000 / 8) : (60000000 / 8);
            break;
        case H265VPS_LEVEL_6_0:
            max_octetrate = max_bs = tier ? (240000000 / 8) : (60000000 / 8);
            break;
        case H265VPS_LEVEL_6_1:
            max_octetrate = max_bs = tier ? (480000000 / 8) : (120000000 / 8);
            break;
        default:
            upipe_warn_va(upipe, "unknown level %"PRIu8,
                          upipe_h265f->level_idc);
            /* fallthrough */
        case H265VPS_LEVEL_6_2:
            max_octetrate = max_bs = tier ? (800000000 / 8) : (240000000 / 8);
            break;
    }
    UBASE_FATAL(upipe,
            uref_block_flow_set_max_octetrate(flow_def, max_octetrate))
    UBASE_FATAL(upipe,
            uref_block_flow_set_max_buffer_size(flow_def, max_bs))

    upipe_h265f_stream_parse_ptl(upipe, s, max_subl_1, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL);
    upipe_h26xf_stream_ue(s); /* sps_id */
    uint32_t chroma_idc = upipe_h265f->chroma_idc = upipe_h26xf_stream_ue(s);
    if (chroma_idc == 3) {
        upipe_h26xf_stream_fill_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1); /* separate_colour_plane */
    }

    uint32_t hsize = upipe_h26xf_stream_ue(s);
    uint32_t vsize = upipe_h26xf_stream_ue(s);

    upipe_h26xf_stream_fill_bits(s, 1);
    bool conformance_window = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (conformance_window) {
        upipe_h26xf_stream_ue(s); /* left offset */
        upipe_h26xf_stream_ue(s); /* right offset */
        upipe_h26xf_stream_ue(s); /* top offset */
        upipe_h26xf_stream_ue(s); /* bottom offset */
    }

    uint32_t junk1 = upipe_h26xf_stream_ue(s); /* bit_depth_luma */
    uint32_t junk2 = upipe_h26xf_stream_ue(s); /* bit_depth_chroma */

    uint32_t log2_max_pic_order_cnt = upipe_h26xf_stream_ue(s) + 4;
    if (log2_max_pic_order_cnt > 16) {
        upipe_err_va(upipe, "invalid SPS (max_pic_order_cnt %"PRIu32")",
                     log2_max_pic_order_cnt);
        ubuf_block_stream_clean(s);
        return false;
    }

    upipe_h26xf_stream_fill_bits(s, 1);
    bool subl_ordering = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    uint32_t max_dec_pic_buffering_1 = 0;
    for (int i = (subl_ordering ? 0 : max_subl_1); i <= max_subl_1; i++) {
        /* select the last one */
        max_dec_pic_buffering_1 = upipe_h26xf_stream_ue(s);
        upipe_h26xf_stream_ue(s); /* max_num_reorder_pics */
        upipe_h26xf_stream_ue(s); /* max_latency_increase */
    }

    upipe_h26xf_stream_ue(s); /* min_luma_coding_block_size */
    upipe_h26xf_stream_ue(s); /* diff_max_min_luma_coding_block_size */
    upipe_h26xf_stream_ue(s); /* min_transport_block_size */
    upipe_h26xf_stream_ue(s); /* diff_max_min_transport_block_size */
    upipe_h26xf_stream_ue(s); /* max_transform_hierarchy_depth_inter */
    upipe_h26xf_stream_ue(s); /* max_transform_hierarchy_depth_intra */

    upipe_h26xf_stream_fill_bits(s, 1);
    bool scaling_list = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (scaling_list) {
        upipe_h26xf_stream_fill_bits(s, 1);
        bool scaling_list_data = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);

        if (scaling_list_data)
            upipe_h265f_stream_parse_scaling(s);
    }

    upipe_h26xf_stream_fill_bits(s, 3);
    ubuf_block_stream_skip_bits(s, 2);
    bool pcm_enabled = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (pcm_enabled) {
        upipe_h26xf_stream_fill_bits(s, 8);
        ubuf_block_stream_skip_bits(s, 8);
        upipe_h26xf_stream_ue(s);
        upipe_h26xf_stream_ue(s);
        upipe_h26xf_stream_fill_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
    }

    uint32_t max_short_term_ref_pic_sets = upipe_h26xf_stream_ue(s);
    uint32_t num_delta_pocs[max_short_term_ref_pic_sets];
    memset(num_delta_pocs, 0, sizeof(num_delta_pocs));
    for (int i = 0; i < max_short_term_ref_pic_sets; i++) {
        if (!upipe_h265f_stream_parse_short_term_ref_pic_set(s, i,
                max_short_term_ref_pic_sets, max_dec_pic_buffering_1,
                num_delta_pocs)) {
            upipe_err(upipe, "invalid SPS (short_term_ref_pic_sets)");
            ubuf_block_stream_clean(s);
            return false; 
        }
    }

    upipe_h26xf_stream_fill_bits(s, 1);
    bool long_term_ref_pics = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (long_term_ref_pics) {
        uint32_t num_long_term_ref_pics = upipe_h26xf_stream_ue(s);
        for (int i = 0; i < num_long_term_ref_pics; i++) {
            upipe_h26xf_stream_fill_bits(s, log2_max_pic_order_cnt + 1);
            ubuf_block_stream_skip_bits(s, log2_max_pic_order_cnt + 1);
        }
    }

    upipe_h26xf_stream_fill_bits(s, 3);
    ubuf_block_stream_skip_bits(s, 2);
    bool vui = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);

    bool field_seq_flag = false;
    uint8_t video_format = 5;
    bool full_range = false;
    uint8_t colour_primaries = 2;
    uint8_t transfer_characteristics = 2;
    uint8_t matrix_coefficients = 2;
    uint32_t time_scale = upipe_h265f->time_scale;
    struct urational frame_rate = upipe_h265f->frame_rate;
    uint64_t octet_rate = upipe_h265f->octet_rate;
    uint64_t cpb_size = upipe_h265f->cpb_size;

    if (vui) {
        upipe_h26xf_stream_fill_bits(s, 1);
        bool ar_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        if (ar_present) {
            upipe_h26xf_stream_fill_bits(s, 8);
            uint8_t ar_idc = ubuf_block_stream_show_bits(s, 8);
            ubuf_block_stream_skip_bits(s, 8);
            if (ar_idc > 0 &&
                ar_idc < sizeof(upipe_h26xf_sar_from_idc) /
                         sizeof(struct urational)) {
                UBASE_FATAL(upipe, uref_pic_flow_set_sar(flow_def,
                            upipe_h26xf_sar_from_idc[ar_idc]));
            } else if (ar_idc == H265VUI_AR_EXTENDED) {
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
            upipe_h26xf_stream_ue(s); /* top_field */
            upipe_h26xf_stream_ue(s); /* bottom_field */
        }

        upipe_h26xf_stream_fill_bits(s, 4);
        ubuf_block_stream_skip_bits(s, 1);
        field_seq_flag = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        upipe_h265f->frame_field_present = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);
        bool default_display_window = !!ubuf_block_stream_show_bits(s, 1);
        ubuf_block_stream_skip_bits(s, 1);

        if (default_display_window) {
            upipe_h26xf_stream_ue(s); /* left */
            upipe_h26xf_stream_ue(s); /* right */
            upipe_h26xf_stream_ue(s); /* top */
            upipe_h26xf_stream_ue(s); /* bottom */
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
            time_scale = ubuf_block_stream_show_bits(s, 16) << 16;
            ubuf_block_stream_skip_bits(s, 16);

            upipe_h26xf_stream_fill_bits(s, 17);
            time_scale |= ubuf_block_stream_show_bits(s, 16);
            ubuf_block_stream_skip_bits(s, 16);
            bool poc_proportional_to_timing = ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            if (poc_proportional_to_timing) {
                uint32_t num_ticks_poc_diff = upipe_h26xf_stream_ue(s) + 1;
                frame_rate.num = time_scale;
                frame_rate.den = num_units_in_ticks * num_ticks_poc_diff;
            }

            bool hrd_present = ubuf_block_stream_show_bits(s, 1);
            ubuf_block_stream_skip_bits(s, 1);
            if (hrd_present) {
                if (!ubase_check(upipe_h265f_stream_parse_hrd(upipe, s,
                                 &octet_rate, &cpb_size))) {
                    ubuf_block_stream_clean(s);
                    return false;
                }
            }
        }

        /* incomplete parsing */
    } else
        upipe_h265f->frame_field_present =
            upipe_h265f->general_progressive && upipe_h265f->general_interlaced;

    UBASE_FATAL(upipe, uref_pic_flow_set_hsize(flow_def, hsize))
    if (field_seq_flag) {
        UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def, vsize * 2))
    } else {
        UBASE_FATAL(upipe, uref_pic_flow_set_vsize(flow_def, vsize))
    }

    if (frame_rate.num) {
        upipe_h265f->duration = UCLOCK_FREQ * frame_rate.den / frame_rate.num;
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                    upipe_h265f->input_latency + upipe_h265f->duration))

        if (field_seq_flag)
            frame_rate.den *= 2;
        urational_simplify(&frame_rate);
        UBASE_FATAL(upipe, uref_pic_flow_set_fps(flow_def, frame_rate))
    }

    if (octet_rate) {
        UBASE_FATAL(upipe, uref_block_flow_set_octetrate(flow_def, octet_rate))
    }
    if (cpb_size) {
        UBASE_FATAL(upipe, uref_block_flow_set_buffer_size(flow_def, cpb_size))
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

    upipe_h265f->active_sps = sps_id;
    ubuf_block_stream_clean(s);

    upipe_h265f_store_flow_def(upipe, NULL);
    uref_free(upipe_h265f->flow_def_requested);
    upipe_h265f->flow_def_requested = NULL;
    flow_def = upipe_h265f_store_flow_def_attr(upipe, flow_def);
    if (flow_def != NULL)
        upipe_h265f_require_flow_format(upipe, flow_def);
    return true;
}

/** @internal @This activates a picture parameter set.
 *
 * @param upipe description structure of the pipe
 * @param pps_id PPS to activate
 * @return false if the PPS couldn't be activated
 */
static bool upipe_h265f_activate_pps(struct upipe *upipe, uint32_t pps_id)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (likely(upipe_h265f->active_pps == pps_id))
        return true;
    if (unlikely(upipe_h265f->pps[pps_id] == NULL)) {
        upipe_err(upipe, "invalid PPS ID");
        return false;
    }

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, upipe_h265f->pps[pps_id], 2))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }

    upipe_h26xf_stream_ue(s); /* pps_id */
    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    if (unlikely(sps_id >= H265SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32, sps_id);
        ubuf_block_stream_clean(s);
        return false;
    }

    if (!upipe_h265f_activate_sps(upipe, sps_id)) {
        ubuf_block_stream_clean(s);
        return false;
    }

    upipe_h26xf_stream_fill_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 2);
    upipe_h265f->num_extra_slice_header_bits =
        ubuf_block_stream_show_bits(s, 3);
    ubuf_block_stream_skip_bits(s, 3);

    upipe_h265f->active_pps = pps_id;
    ubuf_block_stream_clean(s);
    return true;
}

/** @internal @This handles a video parameter set.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @return an error code
 */
static int upipe_h265f_handle_vps(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    upipe_h265f->vps_rap = upipe_h265f->dts_rap;

    ubuf = ubuf_block_splice(ubuf, offset, size);
    if (unlikely(ubuf == NULL))
        return UBASE_ERR_ALLOC;

    uint8_t buffer;
    if (!ubase_check(ubuf_block_extract(ubuf, 2, 1, &buffer))) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }
    uint8_t vps_id = buffer >> 4;

    if (upipe_h265f->active_vps == vps_id &&
        !ubase_check(ubuf_block_equal(upipe_h265f->vps[vps_id], ubuf)))
        upipe_h265f->active_vps = -1;

    if (upipe_h265f->vps[vps_id] != NULL)
        ubuf_free(upipe_h265f->vps[vps_id]);
    upipe_h265f->vps[vps_id] = ubuf;
    return UBASE_ERR_NONE;
}

/** @internal @This handles a sequence parameter set.
 *
 * @param upipe description structure of the pipe
 * @param ubuf ubuf containing the NAL unit
 * @param offset offset of the NAL unit in the ubuf
 * @param size size of the NAL unit, in octets
 * @return an error code
 */
static int upipe_h265f_handle_sps(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    upipe_h265f->sps_rap = upipe_h265f->vps_rap;

    ubuf = ubuf_block_splice(ubuf, offset, size);
    if (unlikely(ubuf == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, ubuf, 2))) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }

    upipe_h26xf_stream_fill_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 4); /* vps_id */
    uint8_t max_subl_1 = ubuf_block_stream_show_bits(s, 3);
    ubuf_block_stream_skip_bits(s, 4); /* temporal_id_nesting_flag */

    upipe_h265f_stream_parse_ptl(upipe, s, max_subl_1, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL);

    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(sps_id >= H265SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32, sps_id);
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }

    if (upipe_h265f->active_vps == -1 ||
        (upipe_h265f->active_sps == sps_id &&
         !ubase_check(ubuf_block_equal(upipe_h265f->sps[sps_id], ubuf)))) {
        upipe_h265f->active_sps = -1;
    }

    if (upipe_h265f->sps[sps_id] != NULL)
        ubuf_free(upipe_h265f->sps[sps_id]);
    upipe_h265f->sps[sps_id] = ubuf;
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
static int upipe_h265f_handle_pps(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    upipe_h265f->pps_rap = upipe_h265f->sps_rap;

    ubuf = ubuf_block_splice(ubuf, offset, size);
    if (unlikely(ubuf == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (!ubase_check(ubuf_block_stream_init(s, ubuf, 2))) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }
    uint32_t pps_id = upipe_h26xf_stream_ue(s);
    ubuf_block_stream_clean(s);

    if (unlikely(pps_id >= H265PPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid PPS %"PRIu32, pps_id);
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }

    if (upipe_h265f->active_vps == -1 || upipe_h265f->active_sps == -1 ||
        (upipe_h265f->active_pps == pps_id &&
         !ubase_check(ubuf_block_equal(upipe_h265f->pps[pps_id], ubuf)))) {
        upipe_h265f->active_pps = -1;
    }

    if (upipe_h265f->pps[pps_id] != NULL)
        ubuf_free(upipe_h265f->pps[pps_id]);
    upipe_h265f->pps[pps_id] = ubuf;
    return UBASE_ERR_NONE;
}

/** @internal @This handles the supplemental enhancement information called
 * buffering period.
 *
 * @param upipe description structure of the pipe
 * @param s block stream parsing structure
 * @return an error code
 */
static int upipe_h265f_handle_sei_buffering_period(struct upipe *upipe,
                                                   struct ubuf_block_stream *s)
{
    uint32_t sps_id = upipe_h26xf_stream_ue(s);
    if (unlikely(sps_id >= H265SPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid SPS %"PRIu32" in SEI", sps_id);
        return UBASE_ERR_INVALID;
    }

    if (!upipe_h265f_activate_sps(upipe, sps_id))
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
static int upipe_h265f_handle_sei_pic_timing(struct upipe *upipe,
                                             struct ubuf_block_stream *s)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (unlikely(upipe_h265f->active_sps == -1)) {
        upipe_warn(upipe, "discarding early picture timing SEI");
        return UBASE_ERR_NONE;
    }

    if (upipe_h265f->frame_field_present) {
        upipe_h26xf_stream_fill_bits(s, 4);
        upipe_h265f->pic_struct = ubuf_block_stream_show_bits(s, 4);
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
static int upipe_h265f_handle_sei(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    uint8_t type;
    if (unlikely(!ubase_check(ubuf_block_extract(ubuf, offset + 2, 1, &type))))
        return UBASE_ERR_INVALID;
    if (type != H265SEI_BUFFERING_PERIOD && type != H265SEI_PIC_TIMING)
        return UBASE_ERR_NONE;

    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    UBASE_RETURN(ubuf_block_stream_init(s, ubuf, offset + 3))

    /* size field */
    uint8_t octet;
    do {
        upipe_h26xf_stream_fill_bits(s, 8);
        octet = ubuf_block_stream_show_bits(s, 8);
        ubuf_block_stream_skip_bits(s, 8);
    } while (octet == UINT8_MAX);

    int err = UBASE_ERR_NONE;
    switch (type) {
        case H265SEI_BUFFERING_PERIOD:
            err = upipe_h265f_handle_sei_buffering_period(upipe, s);
            break;
        case H265SEI_PIC_TIMING:
            err = upipe_h265f_handle_sei_pic_timing(upipe, s);
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
static int upipe_h265f_handle_slice(struct upipe *upipe, struct ubuf *ubuf,
                                    size_t offset, size_t size, uint8_t nal,
                                    bool *au_slice_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    struct upipe_h26xf_stream f;
    upipe_h26xf_stream_init(&f);
    struct ubuf_block_stream *s = &f.s;
    if (unlikely(!ubase_check(ubuf_block_stream_init(s, ubuf, offset + 2))))
        return UBASE_ERR_INVALID;

    upipe_h26xf_stream_fill_bits(s, 2);
    bool first_slice_in_pic = !!ubuf_block_stream_show_bits(s, 1);
    ubuf_block_stream_skip_bits(s, 1);
    if (*au_slice_p && first_slice_in_pic) {
        ubuf_block_stream_clean(s);
        return UBASE_ERR_BUSY;
    }

    uint8_t last_nal_type = h265nalst_get_type(nal);
    if (last_nal_type >= H265NAL_TYPE_BLA_W_LP &&
        last_nal_type <= H265NAL_TYPE_IRAP_VCL23)
        ubuf_block_stream_skip_bits(s, 1);

    uint32_t pps_id = upipe_h26xf_stream_ue(s);
    if (unlikely(pps_id >= H265PPS_ID_MAX)) {
        upipe_warn_va(upipe, "invalid PPS %"PRIu32" in slice", pps_id);
        ubuf_block_stream_clean(s);
        return UBASE_ERR_INVALID;
    }
    if (*au_slice_p && pps_id != upipe_h265f->active_pps) {
        ubuf_block_stream_clean(s);
        return UBASE_ERR_BUSY;
    }
    if (unlikely(!upipe_h265f_activate_pps(upipe, pps_id))) {
        ubuf_block_stream_clean(s);
        return UBASE_ERR_INVALID;
    }

    if (first_slice_in_pic) {
        upipe_h26xf_stream_fill_bits(s, 8);
        ubuf_block_stream_skip_bits(s,
                upipe_h265f->num_extra_slice_header_bits);
        upipe_h265f->slice_type = upipe_h26xf_stream_ue(s);
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
static int upipe_h265f_handle_nal(struct upipe *upipe, struct ubuf *ubuf,
                                  size_t offset, size_t size, bool *au_slice_p,
                                  uint8_t *nal_p)
{
    uint8_t nal;
    if (unlikely(!ubase_check(ubuf_block_extract(ubuf, offset, 1, &nal))))
        return UBASE_ERR_INVALID;
    if (nal_p != NULL)
        *nal_p = nal;

    upipe_verbose_va(upipe, "handling NAL %"PRIu8, h265nalst_get_type(nal));
    if (h265nalst_get_type(nal) < H265NAL_TYPE_VPS)
        return upipe_h265f_handle_slice(upipe, ubuf, offset, size, nal,
                                        au_slice_p);

    switch (h265nalst_get_type(nal)) {
        case H265NAL_TYPE_PREF_SEI:
            return upipe_h265f_handle_sei(upipe, ubuf, offset, size);
            break;
        case H265NAL_TYPE_VPS:
            return upipe_h265f_handle_vps(upipe, ubuf, offset, size);
            break;
        case H265NAL_TYPE_SPS:
            return upipe_h265f_handle_sps(upipe, ubuf, offset, size);
            break;
        case H265NAL_TYPE_PPS:
            return upipe_h265f_handle_pps(upipe, ubuf, offset, size);
            break;
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
static void upipe_h265f_handle_global_annexb(struct upipe *upipe,
                                             const uint8_t *p, size_t size)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (upipe_h265f->encaps_input != UREF_H26X_ENCAPS_ANNEXB) {
        upipe_warn_va(upipe,
                      "fixing up input encapsulation to annex B (from %d)",
                      upipe_h265f->encaps_input);
        upipe_h265f->encaps_input = UREF_H26X_ENCAPS_ANNEXB;
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

        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_h265f->ubuf_mgr,
                                                         p + b + s, e - b - s);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_verbose_va(upipe, "found global header NAL %"PRIu8" of size %d",
                         h265nalst_get_type(p[b + s]), e - b - s);

        int err = upipe_h265f_handle_nal(upipe, ubuf, 0, e - b - s, &au_slice,
                                         NULL);
        if (!ubase_check(err)) {
            upipe_err(upipe, "invalid global NAL received");
            upipe_throw_fatal(upipe, err);
        }
        ubuf_free(ubuf);

        b = e;
    } while (b < size - 3);
}

/** @internal @This uses hvcC global headers.
 *
 * @param upipe description structure of the pipe
 * @param p pointer to global headers
 * @param size size of global headers
 */
static void upipe_h265f_handle_global_hvcc(struct upipe *upipe,
                                           const uint8_t *p, size_t size)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (!h265hvcc_validate(p, size)) {
        upipe_err_va(upipe, "invalid hvcC structure (size %zu)", size);
        return;
    }

    uint8_t length_size = h265hvcc_get_length_size_1(p) + 1;
    enum uref_h26x_encaps encaps_input;
    switch (length_size) {
        case 1: encaps_input = UREF_H26X_ENCAPS_LENGTH1; break;
        case 2: encaps_input = UREF_H26X_ENCAPS_LENGTH2; break;
        case 4: encaps_input = UREF_H26X_ENCAPS_LENGTH4; break;
        default:
            upipe_err(upipe, "invalid length size in hvcC");
            return;
    }

    if (upipe_h265f->encaps_input != encaps_input) {
        upipe_warn_va(upipe,
                "fixing up input encapsulation to length%"PRIu8" (from %d)",
                length_size, upipe_h265f->encaps_input);
        upipe_h265f->encaps_input = encaps_input;
    }

    bool au_slice = false;
    uint8_t arrays = h265hvcc_get_num_of_arrays(p);
    for (uint8_t array = 0; array < arrays; array++) {
        const uint8_t *a = h265hvcc_get_array(p, array);
        uint8_t nalus = h265hvcc_array_get_num_nalus(a);

        for (uint8_t nalu = 0; nalu < nalus; nalu++) {
            const uint8_t *n = h265hvcc_array_get_nalu(a, nalu);
            uint16_t length = h265hvcc_nalu_get_length(n);
            const uint8_t *buffer = h265hvcc_nalu_get_nalu(n);

            struct ubuf *ubuf =
                ubuf_block_alloc_from_opaque(upipe_h265f->ubuf_mgr,
                                             buffer, length);
            if (unlikely(ubuf == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }

            int err = upipe_h265f_handle_nal(upipe, ubuf, 0, length, &au_slice,
                                             NULL);
            if (!ubase_check(err)) {
                upipe_err(upipe, "invalid global NAL received");
                upipe_throw_fatal(upipe, err);
            }
            ubuf_free(ubuf);
        }
    }
}

/** @internal @This checks if there are global headers and uses them.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h265f_handle_global(struct upipe *upipe)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    const uint8_t *p;
    size_t size;
    if (!ubase_check(uref_flow_get_headers(upipe_h265f->flow_def_input,
                                           &p, &size)) || size < 6)
        return;

    /* We use the second octet of the startcode, because the hvcC version
     * may be 0. */
    if (p[1] == 0)
        upipe_h265f_handle_global_annexb(upipe, p, size);
    else
        upipe_h265f_handle_global_hvcc(upipe, p, size);
}

/** @internal @This builds the global headers of the flow definition packet.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 */
static void upipe_h265f_build_global(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    size_t headers_size = 0;
    unsigned int nb_vps = 0;
    unsigned int nb_sps = 0;
    unsigned int nb_pps = 0;
    int i;
    for (i = 0; i < H265VPS_ID_MAX; i++) {
        if (upipe_h265f->vps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h265f->vps[i], &ubuf_size);
            headers_size += ubuf_size;
            nb_vps++;
        }
    }
    for (i = 0; i < H265SPS_ID_MAX; i++) {
        if (upipe_h265f->sps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h265f->sps[i], &ubuf_size);
            headers_size += ubuf_size;
            nb_sps++;
        }
    }
    for (i = 0; i < H265PPS_ID_MAX; i++) {
        if (upipe_h265f->pps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h265f->pps[i], &ubuf_size);
            headers_size += ubuf_size;
            nb_pps++;
        }
    }

    if (upipe_h265f->encaps_output == UREF_H26X_ENCAPS_ANNEXB) {
        uint8_t headers[headers_size + nb_vps * 4 + nb_sps * 4 + nb_pps * 4];
        int j = 0;
        for (i = 0; i < H265VPS_ID_MAX; i++) {
            if (upipe_h265f->vps[i] != NULL) {
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 1;
                size_t ubuf_size = 0;
                ubuf_block_size(upipe_h265f->vps[i], &ubuf_size);
                UBASE_FATAL(upipe,
                        ubuf_block_extract(upipe_h265f->vps[i], 0, -1,
                                           headers + j))
                j += ubuf_size;
            }
        }
        for (i = 0; i < H265SPS_ID_MAX; i++) {
            if (upipe_h265f->sps[i] != NULL) {
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 1;
                size_t ubuf_size = 0;
                ubuf_block_size(upipe_h265f->sps[i], &ubuf_size);
                UBASE_FATAL(upipe,
                        ubuf_block_extract(upipe_h265f->sps[i], 0, -1,
                                           headers + j))
                j += ubuf_size;
            }
        }
        for (i = 0; i < H265PPS_ID_MAX; i++) {
            if (upipe_h265f->pps[i] != NULL) {
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 0;
                headers[j++] = 1;
                size_t ubuf_size = 0;
                ubuf_block_size(upipe_h265f->pps[i], &ubuf_size);
                UBASE_FATAL(upipe,
                        ubuf_block_extract(upipe_h265f->pps[i], 0, -1,
                                           headers + j))
                j += ubuf_size;
            }
        }
        UBASE_FATAL(upipe, uref_flow_set_headers(flow_def, headers, j))
        return;
    }

    /* build hvcC */
    uint8_t length_size;
    switch (upipe_h265f->encaps_output) {
        case UREF_H26X_ENCAPS_LENGTH1: length_size = 1; break;
        case UREF_H26X_ENCAPS_LENGTH2: length_size = 2; break;
        default:
        case UREF_H26X_ENCAPS_LENGTH4: length_size = 4; break;
    }

    uint8_t headers[H265HVCC_HEADER + headers_size + 3 * H265HVCC_ARRAY_HEADER +
                    nb_vps * H265HVCC_NALU_HEADER +
                    nb_sps * H265HVCC_NALU_HEADER +
                    nb_pps * H265HVCC_NALU_HEADER];
    h265hvcc_init(headers);
    h265hvcc_set_profile_space(headers, upipe_h265f->profile_space);
    if (upipe_h265f->tier)
        h265hvcc_set_tier(headers);
    h265hvcc_set_profile_idc(headers, upipe_h265f->profile_idc);
    h265hvcc_set_profile_compatibility(headers,
            upipe_h265f->profile_compatibility);
    h265hvcc_set_constraint_indicator(headers,
            upipe_h265f->constraint_indicator);
    h265hvcc_set_level_idc(headers, upipe_h265f->level_idc);
    h265hvcc_set_chroma_format(headers, upipe_h265f->chroma_idc);
    h265hvcc_set_length_size_1(headers, length_size - 1);
    h265hvcc_set_num_of_arrays(headers, 3);

    uint8_t *a = h265hvcc_get_array(headers, 0);
    h265hvcc_array_set_nal_unit_type(a, H265NAL_TYPE_VPS);
    h265hvcc_array_set_num_nalus(a, nb_vps);
    uint8_t n = 0;
    for (i = 0; i < H265VPS_ID_MAX; i++) {
        if (upipe_h265f->vps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h265f->vps[i], &ubuf_size);
            assert(ubuf_size <= UINT16_MAX);
            uint8_t *p = h265hvcc_array_get_nalu(headers, n++);
            h265hvcc_nalu_set_length(p, ubuf_size);
            p = h265hvcc_nalu_get_nalu(p);
            UBASE_FATAL(upipe,
                    ubuf_block_extract(upipe_h265f->vps[i], 0, -1, p))
        }
    }

    a = h265hvcc_get_array(headers, 1);
    h265hvcc_array_set_nal_unit_type(a, H265NAL_TYPE_SPS);
    h265hvcc_array_set_num_nalus(a, nb_sps);
    n = 0;
    for (i = 0; i < H265SPS_ID_MAX; i++) {
        if (upipe_h265f->sps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h265f->sps[i], &ubuf_size);
            assert(ubuf_size <= UINT16_MAX);
            uint8_t *p = h265hvcc_array_get_nalu(headers, n++);
            h265hvcc_nalu_set_length(p, ubuf_size);
            p = h265hvcc_nalu_get_nalu(p);
            UBASE_FATAL(upipe,
                    ubuf_block_extract(upipe_h265f->sps[i], 0, -1, p))
        }
    }

    a = h265hvcc_get_array(headers, 2);
    h265hvcc_array_set_nal_unit_type(a, H265NAL_TYPE_PPS);
    h265hvcc_array_set_num_nalus(a, nb_pps);
    n = 0;
    for (i = 0; i < H265PPS_ID_MAX; i++) {
        if (upipe_h265f->pps[i] != NULL) {
            size_t ubuf_size = 0;
            ubuf_block_size(upipe_h265f->pps[i], &ubuf_size);
            assert(ubuf_size <= UINT16_MAX);
            uint8_t *p = h265hvcc_array_get_nalu(headers, n++);
            h265hvcc_nalu_set_length(p, ubuf_size);
            p = h265hvcc_nalu_get_nalu(p);
            UBASE_FATAL(upipe,
                    ubuf_block_extract(upipe_h265f->pps[i], 0, -1, p))
        }
    }

    a = h265hvcc_get_array(headers, 3);
    UBASE_FATAL(upipe, uref_flow_set_headers(flow_def, headers, a - headers))
}

/** @internal @This builds the flow definition packet.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h265f_build_flow_def(struct upipe *upipe)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    assert(upipe_h265f->flow_def_requested != NULL);

    struct uref *flow_def = uref_dup(upipe_h265f->flow_def_requested);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    const uint8_t *p;
    size_t size;
    if (!ubase_check(uref_flow_get_global(flow_def)))
        uref_flow_delete_headers(flow_def);
    else if (!ubase_check(uref_flow_get_headers(flow_def, &p, &size)) ||
             upipe_h265f->encaps_input != upipe_h265f->encaps_output)
        upipe_h265f_build_global(upipe, flow_def);

    if (upipe_h265f->duration) {
        UBASE_FATAL(upipe, uref_clock_set_latency(flow_def,
                         upipe_h265f->input_latency + upipe_h265f->duration))
    }

    upipe_h265f_store_flow_def(upipe, flow_def);
    /* force sending flow definition immediately */
    upipe_h265f_output(upipe, NULL, NULL);
}

/** @internal @This prepares an access unit for output.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @return an error code
 */
static int upipe_h265f_prepare_au(struct upipe *upipe, struct uref *uref)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    uint64_t duration = upipe_h265f->duration;
    if (upipe_h265f->pic_struct == -1)
        upipe_h265f->pic_struct = H265SEI_STRUCT_FRAME;

    switch (upipe_h265f->pic_struct) {
        case H265SEI_STRUCT_FRAME:
            UBASE_FATAL(upipe, uref_pic_set_progressive(uref))
            duration *= 2;
            break;
        case H265SEI_STRUCT_TOP:
        case H265SEI_STRUCT_TOP_PREV_BOT:
        case H265SEI_STRUCT_TOP_NEXT_BOT:
            UBASE_FATAL(upipe, uref_pic_set_tf(uref))
            break;
        case H265SEI_STRUCT_BOT:
        case H265SEI_STRUCT_BOT_PREV_TOP:
        case H265SEI_STRUCT_BOT_NEXT_TOP:
            UBASE_FATAL(upipe, uref_pic_set_bf(uref))
            break;
        case H265SEI_STRUCT_TOP_BOT:
            UBASE_FATAL(upipe, uref_pic_set_tf(uref))
            UBASE_FATAL(upipe, uref_pic_set_bf(uref))
            UBASE_FATAL(upipe, uref_pic_set_tff(uref))
            duration *= 2;
            break;
        case H265SEI_STRUCT_BOT_TOP:
            UBASE_FATAL(upipe, uref_pic_set_tf(uref))
            UBASE_FATAL(upipe, uref_pic_set_bf(uref))
            duration *= 2;
            break;
        case H265SEI_STRUCT_TOP_BOT_TOP:
            UBASE_FATAL(upipe, uref_pic_set_tf(uref))
            UBASE_FATAL(upipe, uref_pic_set_bf(uref))
            UBASE_FATAL(upipe, uref_pic_set_tff(uref))
            duration *= 3;
            break;
        case H265SEI_STRUCT_BOT_TOP_BOT:
            UBASE_FATAL(upipe, uref_pic_set_tf(uref))
            UBASE_FATAL(upipe, uref_pic_set_bf(uref))
            duration *= 3;
            break;
        case H265SEI_STRUCT_DOUBLE:
            duration *= 4;
            break;
        case H265SEI_STRUCT_TRIPLE:
            duration *= 6;
            break;
        default:
            upipe_warn_va(upipe, "invalid picture structure %"PRId32,
                          upipe_h265f->pic_struct);
            break;
    }
    if (duration)
        UBASE_FATAL(upipe, uref_clock_set_duration(uref, duration))

    UBASE_FATAL(upipe, uref_h265_set_type(uref, upipe_h265f->slice_type))
    switch (upipe_h265f->slice_type) {
        case H265SLI_TYPE_I:
            upipe_h265f->iframe_rap = upipe_h265f->pps_rap;
            UBASE_FATAL(upipe, uref_pic_set_key(uref))
            break;
        default:
            break;
    }

    if (upipe_h265f->iframe_rap != UINT64_MAX)
        if (!ubase_check(uref_clock_set_rap_sys(uref, upipe_h265f->iframe_rap)))
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
static int64_t upipe_h265f_find_annexb_nal(struct upipe *upipe,
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
            nal = h265nalst_get_type(startcode[3]);
        else
            nal = h265nalst_get_type(startcode[4]);
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
static void upipe_h265f_output_au(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);

    int err = upipe_h26xf_convert_frame(uref,
            upipe_h265f->encaps_input, upipe_h265f->encaps_output,
            upipe_h265f->ubuf_mgr, upipe_h265f->annexb_header);
    if (!ubase_check(err)) {
        upipe_throw_error(upipe, err);
        uref_free(uref);
        return;
    }

    if (upipe_h265f->encaps_output != UREF_H26X_ENCAPS_ANNEXB) {
        upipe_h265f_output(upipe, uref, upump_p);
        return;
    }

    if (ubase_check(uref_pic_get_key(uref)) &&
        upipe_h265f_find_annexb_nal(upipe, uref, H265NAL_TYPE_VPS) == -1) {
        upipe_verbose(upipe, "prepending VPS, SPS and PPS on keyframe");

        struct ubuf *ubuf = ubuf_dup(upipe_h265f->annexb_header);
        struct ubuf *ubuf2 =
            ubuf_dup(upipe_h265f->pps[upipe_h265f->active_pps]);
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

        ubuf = ubuf_dup(upipe_h265f->annexb_header);
        ubuf2 = ubuf_dup(upipe_h265f->sps[upipe_h265f->active_sps]);
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

        ubuf = ubuf_dup(upipe_h265f->annexb_header);
        ubuf2 = ubuf_dup(upipe_h265f->vps[upipe_h265f->active_vps]);
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

    if (upipe_h265f_find_annexb_nal(upipe, uref, H265NAL_TYPE_AUD) == -1) {
        upipe_verbose(upipe, "prepending AUD");
        struct ubuf *ubuf = ubuf_dup(upipe_h265f->annexb_aud);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        int err = uref_h26x_prepend_nal(uref, ubuf);
        if (unlikely(!ubase_check(err)))
            upipe_throw_error(upipe, err);
    }

    upipe_h265f_output(upipe, uref, upump_p);
}

/** @internal @This prepares an annex B access unit.
 *
 * @param upipe description structure of the pipe
 * @return pointer to uref
 */
static struct uref *upipe_h265f_prepare_annexb(struct upipe *upipe)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (!upipe_h265f->au_size) {
        upipe_h265f->au_nal_units = 0;
        upipe_h265f->au_vcl_offset = -1;
        upipe_h265f->au_slice = false;
        upipe_h265f->pic_struct = -1;
        return NULL;
    }
    if (upipe_h265f->active_vps == -1 || upipe_h265f->active_sps == -1 ||
        upipe_h265f->active_pps == -1) {
        upipe_warn(upipe, "discarding data without VPS/SPS/PPS");
        upipe_h265f_consume_uref_stream(upipe, upipe_h265f->au_size);
        upipe_h265f->au_size = 0;
        upipe_h265f->au_nal_units = 0;
        upipe_h265f->au_vcl_offset = -1;
        upipe_h265f->au_slice = false;
        upipe_h265f->pic_struct = -1;
        return NULL;
    }

    struct uref au_uref_s = upipe_h265f->au_uref_s;
    struct urational drift_rate = upipe_h265f->drift_rate;
    /* From now on, PTS declaration only impacts the next frame. */
    upipe_h265f_flush_dates(upipe);

    struct uref *uref = upipe_h265f_extract_uref_stream(upipe,
                                                        upipe_h265f->au_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }
    upipe_h265f->au_nal_units = 0;

    int err = upipe_h265f_prepare_au(upipe, uref);
    UBASE_FATAL(upipe, err);

    if (upipe_h265f->au_vcl_offset > 0)
        UBASE_FATAL(upipe, uref_block_set_header_size(uref,
                    upipe_h265f->au_vcl_offset))

    uint64_t duration = 0;
    uref_clock_get_duration(uref, &duration);

    /* We work on encoded data so in the DTS domain. Rebase on DTS. */
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(&au_uref_s, &date))) {          \
        uref_clock_set_dts_##dv(uref, date);                                \
        if (!ubase_check(uref_clock_get_dts_##dv(&upipe_h265f->au_uref_s,   \
                                                 NULL)))                    \
            uref_clock_set_dts_##dv(&upipe_h265f->au_uref_s,                \
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

    upipe_h265f->au_size = 0;
    upipe_h265f->au_vcl_offset = -1;
    upipe_h265f->au_slice = false;
    upipe_h265f->pic_struct = -1;

    return uref;
}

/** @internal @This outputs the previous access unit, before the current NAL.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h265f_output_prev_annexb(struct upipe *upipe,
                                           struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    size_t slice_size = upipe_h265f->au_size -
                        upipe_h265f->au_last_nal_offset;
    upipe_h265f->au_size = upipe_h265f->au_last_nal_offset;
    struct uref *uref = upipe_h265f_prepare_annexb(upipe);
    upipe_h265f->au_size = slice_size;
    upipe_h265f->au_last_nal_offset = 0;

    if (uref == NULL)
        return;

    if (unlikely(upipe_h265f->flow_def_requested == NULL)) {
        upipe_h265f->uref_output = uref;
        return;
    }

    upipe_h265f_output_au(upipe, uref, upump_p);
}

/** @internal @This is called when a new NAL starts, to check the previous NAL.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h265f_end_annexb(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (unlikely(!upipe_h265f->acquired)) {
        if (upipe_h265f->au_size) {
            /* we need to discard previous data */
            upipe_warn(upipe, "discarding non-sync data");
            upipe_h265f_consume_uref_stream(upipe, upipe_h265f->au_size);
            upipe_h265f->au_size = 0;
        }
        upipe_h265f_sync_acquired(upipe);
        return;
    }
    if (upipe_h265f->au_last_nal_offset == -1)
        return;

    uint8_t last_nal_type = h265nalst_get_type(upipe_h265f->au_last_nal);
    if (last_nal_type < H265NAL_TYPE_VPS) {
        if (unlikely(upipe_h265f->got_discontinuity)) {
            upipe_warn(upipe, "outputting corrupt data");
            uref_flow_set_error(upipe_h265f->next_uref);
        }

        uint64_t nal_offset = upipe_h265f->au_last_nal_offset +
                              upipe_h265f->au_last_nal_start_size - 2;
        int err = upipe_h265f_handle_nal(upipe, upipe_h265f->next_uref->ubuf,
                                         nal_offset,
                                         upipe_h265f->au_size - nal_offset,
                                         &upipe_h265f->au_slice, NULL);
        if (err == UBASE_ERR_BUSY) {
            upipe_h265f_output_prev_annexb(upipe, upump_p);
            nal_offset = upipe_h265f->au_last_nal_offset +
                         upipe_h265f->au_last_nal_start_size - 2,
            err = upipe_h265f_handle_nal(upipe, upipe_h265f->next_uref->ubuf,
                                         nal_offset,
                                         upipe_h265f->au_size - nal_offset,
                                         &upipe_h265f->au_slice, NULL);
        }

        if (!ubase_check(err)) {
            upipe_warn(upipe, "discarding invalid slice data");
            upipe_h265f_consume_uref_stream(upipe, upipe_h265f->au_size);
            upipe_h265f->au_size = 0;
            return;
        }
        if (last_nal_type == H265NAL_TYPE_BLA_W_LP ||
            last_nal_type == H265NAL_TYPE_BLA_W_RADL ||
            last_nal_type == H265NAL_TYPE_BLA_N_LP ||
            last_nal_type == H265NAL_TYPE_IDR_W_RADL ||
            last_nal_type == H265NAL_TYPE_IDR_N_LP ||
            last_nal_type == H265NAL_TYPE_CRA) {
            uref_flow_set_random(upipe_h265f->next_uref);
        }
        return;
    }

    if (upipe_h265f->got_discontinuity) {
        /* discard the entire NAL */
        upipe_warn(upipe, "discarding non-slice data due to discontinuity");
        upipe_h265f_consume_uref_stream(upipe, upipe_h265f->au_size);
        upipe_h265f->au_size = 0;
        return;
    }

    uint64_t nal_offset = upipe_h265f->au_last_nal_offset +
                          upipe_h265f->au_last_nal_start_size - 2;
    int err = upipe_h265f_handle_nal(upipe, upipe_h265f->next_uref->ubuf,
                                     nal_offset,
                                     upipe_h265f->au_size - nal_offset,
                                     &upipe_h265f->au_slice, NULL);
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
static bool upipe_h265f_begin_annexb(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    uint8_t nal_type = h265nalst_get_type(upipe_h265f->au_last_nal);
    if (nal_type < H265NAL_TYPE_VPS) {
        if (upipe_h265f->au_vcl_offset == -1) {
            upipe_h265f->au_vcl_offset =
                upipe_h265f->au_size - upipe_h265f->au_last_nal_start_size;
            return false;
        }
        return false;
    }

    switch (nal_type) {
        case H265NAL_TYPE_PREF_SEI:
            if (upipe_h265f->au_vcl_offset == -1) {
                upipe_h265f->au_vcl_offset =
                    upipe_h265f->au_size - upipe_h265f->au_last_nal_start_size;
                return false;
            }
            if (!upipe_h265f->au_slice)
                return false;
            break;

        case H265NAL_TYPE_EOS:
        case H265NAL_TYPE_EOB:
            return false;

        case H265NAL_TYPE_AUD:
        case H265NAL_TYPE_VPS:
        case H265NAL_TYPE_SPS:
        case H265NAL_TYPE_PPS:
        default:
            if (!upipe_h265f->au_slice)
                return false;
            break;
    }

    upipe_h265f->au_size -= upipe_h265f->au_last_nal_start_size;
    struct uref *uref = upipe_h265f_prepare_annexb(upipe);
    upipe_h265f->au_size = upipe_h265f->au_last_nal_start_size;

    if (uref == NULL)
        return false;

    if (unlikely(upipe_h265f->flow_def_requested == NULL)) {
        upipe_h265f->uref_output = uref;
        return false;
    }

    upipe_h265f_output_au(upipe, uref, upump_p);
    return false;
}

/** @internal @This is called back by @ref upipe_h265f_append_uref_stream
 * whenever a new uref is promoted in next_uref.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h265f_promote_uref(struct upipe *upipe)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    uint64_t date;
#define SET_DATE(dv)                                                        \
    if (ubase_check(uref_clock_get_dts_##dv(upipe_h265f->next_uref, &date)))\
        uref_clock_set_dts_##dv(&upipe_h265f->au_uref_s, date);
    SET_DATE(sys)
    SET_DATE(prog)
    SET_DATE(orig)
#undef SET_DATE

    if (ubase_check(uref_clock_get_dts_pts_delay(upipe_h265f->next_uref,
                                                 &date)))
        uref_clock_set_dts_pts_delay(&upipe_h265f->au_uref_s, date);
    uref_clock_get_rate(upipe_h265f->next_uref, &upipe_h265f->drift_rate);
    if (ubase_check(uref_clock_get_dts_prog(upipe_h265f->next_uref, &date)))
        uref_clock_get_rap_sys(upipe_h265f->next_uref, &upipe_h265f->dts_rap);
}

/** @internal @This finds an H.265 annex B NAL and returns its type.
 *
 * @param upipe description structure of the pipe
 * @param start_p filled in with the value of the start code
 * @param prev_p filled in with the value of the previous octet, if applicable
 * @return true if a start code was found
 */
static bool upipe_h265f_find(struct upipe *upipe,
                             uint8_t *start_p, uint8_t *prev_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    const uint8_t *buffer;
    int size = -1;
    while (ubase_check(uref_block_read(upipe_h265f->next_uref,
                    upipe_h265f->au_size, &size, &buffer))) {
        const uint8_t *p = upipe_framers_mpeg_scan(buffer, buffer + size,
                                                   &upipe_h265f->scan_context);
        if (p > buffer + 5)
            *prev_p = p[-5];
        uref_block_unmap(upipe_h265f->next_uref, upipe_h265f->au_size);

        if ((upipe_h265f->scan_context & 0xffffff00) == 0x100) {
            *start_p = upipe_h265f->scan_context & 0xff;
            upipe_h265f->au_size += p - buffer;

            /* make sure we have the second octet of the NAL header */
            uint8_t junk;
            if (!ubase_check(uref_block_extract(upipe_h265f->next_uref,
                                    upipe_h265f->au_size, 1, &junk))) {
                upipe_h265f->au_size--;
                upipe_h265f->scan_context >>= 8;
                return false;
            }
            upipe_h265f->au_size++;

            /* retrieve the octet preceding the start code, if it exists */
            if (p <= buffer + 6 &&
                !ubase_check(uref_block_extract(upipe_h265f->next_uref,
                                    upipe_h265f->au_size - 6, 1, prev_p)))
                *prev_p = 0xff;
            return true;
        }
        upipe_h265f->au_size += size;
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
static void upipe_h265f_work_annexb(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    while (upipe_h265f->next_uref != NULL) {
        if (upipe_h265f->flow_def_requested == NULL &&
            upipe_h265f->flow_def_attr != NULL)
            return;

        uint8_t start, prev;
        if (!upipe_h265f_find(upipe, &start, &prev))
            break;
        size_t start_size = !prev ? 6 : 5;

        upipe_h265f->au_size -= start_size;
        upipe_h265f_end_annexb(upipe, upump_p);

        if (upipe_h265f->flow_def_requested == NULL &&
            upipe_h265f->flow_def_attr != NULL)
            return;

        if (upipe_h265f->au_size)
            uref_h26x_set_nal_offset(upipe_h265f->next_uref,
                                     upipe_h265f->au_size,
                                     upipe_h265f->au_nal_units++);
        upipe_h265f->au_size += start_size;
        upipe_h265f->got_discontinuity = false;
        upipe_h265f->au_last_nal = start;
        upipe_h265f->au_last_nal_start_size = start_size;
        if (upipe_h265f_begin_annexb(upipe, upump_p))
            upipe_h265f->au_last_nal_offset = -1;
        else
            upipe_h265f->au_last_nal_offset = upipe_h265f->au_size - start_size;
    }

    if (!upipe_h265f->complete_input || !upipe_h265f->au_size)
       return;

    upipe_h265f_end_annexb(upipe, upump_p);
    struct uref *uref = upipe_h265f_prepare_annexb(upipe);

    if (uref == NULL)
        return;

    if (unlikely(upipe_h265f->flow_def_requested == NULL)) {
        upipe_h265f->uref_output = uref;
        return;
    }

    upipe_h265f_output_au(upipe, uref, upump_p);
}

/** @internal @This works on incoming frames in NALU format (supposedly
 * one frame per uref guaranteed by demux).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure containing one frame
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_h265f_work_nalu(struct upipe *upipe, struct uref *uref,
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

        int err = upipe_h265f_handle_nal(upipe, uref->ubuf, nal_offset,
                                         nal_size, &au_slice, &nal);
        if (!ubase_check(err)) {
            upipe_warn(upipe, "invalid NAL received");
            upipe_throw_error(upipe, err);
        }
        uint8_t nal_type = h265nalst_get_type(nal);
        if (nal_type == H265NAL_TYPE_BLA_W_LP ||
            nal_type == H265NAL_TYPE_BLA_W_RADL ||
            nal_type == H265NAL_TYPE_BLA_N_LP ||
            nal_type == H265NAL_TYPE_IDR_W_RADL ||
            nal_type == H265NAL_TYPE_IDR_N_LP ||
            nal_type == H265NAL_TYPE_CRA) {
            uref_flow_set_random(uref);
        }
        if ((nal_type < H265NAL_TYPE_VPS || nal_type == H265NAL_TYPE_PREF_SEI)
                && vcl_offset == -1)
            vcl_offset = nal_offset;
    }

    UBASE_RETURN(uref_block_set_header_size(uref, vcl_offset))

    upipe_h265f_output_au(upipe, uref, upump_p);
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
static bool upipe_h265f_work_length(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    uint64_t nal_offset = 0;
    size_t uref_size;
    if (unlikely(!ubase_check(uref_block_size(uref, &uref_size)))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return true;
    }

    uint8_t length_size;
    switch (upipe_h265f->encaps_input) {
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
        int err = upipe_h265f_handle_nal(upipe, uref->ubuf,
                                         nal_offset + length_size,
                                         nal_size, &au_slice, &nal);
        if (!ubase_check(err)) {
            upipe_warn(upipe, "invalid NAL received");
            upipe_throw_error(upipe, err);
        }
        uint8_t nal_type = h265nalst_get_type(nal);
        if (nal_type == H265NAL_TYPE_BLA_W_LP ||
            nal_type == H265NAL_TYPE_BLA_W_RADL ||
            nal_type == H265NAL_TYPE_BLA_N_LP ||
            nal_type == H265NAL_TYPE_IDR_W_RADL ||
            nal_type == H265NAL_TYPE_IDR_N_LP ||
            nal_type == H265NAL_TYPE_CRA) {
            uref_flow_set_random(uref);
        }
        if ((nal_type < H265NAL_TYPE_VPS || nal_type == H265NAL_TYPE_PREF_SEI)
                && vcl_offset == -1)
            vcl_offset = nal_offset;

        if (nal_offset)
            uref_h26x_set_nal_offset(uref, nal_offset, au_nal_units++);
        nal_offset += length_size + nal_size;
    }

    UBASE_RETURN(uref_block_set_header_size(uref, vcl_offset))

    upipe_h265f_output_au(upipe, uref, upump_p);
    return true;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the packet was handled
 */
static bool upipe_h265f_handle(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_h265f->input_latency = 0;
        uref_clock_get_latency(uref, &upipe_h265f->input_latency);
        upipe_h265f->encaps_input = uref_h26x_flow_infer_encaps(uref);
        upipe_h265f->complete_input = ubase_check(uref_flow_get_complete(uref));
        upipe_h265f_store_flow_def(upipe, NULL);
        uref_free(upipe_h265f->flow_def_requested);
        upipe_h265f->flow_def_requested = NULL;
        uref = upipe_h265f_store_flow_def_input(upipe, uref);
        if (uref != NULL)
            upipe_h265f_require_flow_format(upipe, uref);
        upipe_h265f_handle_global(upipe);
        return true;
    }

    if (upipe_h265f->flow_def_requested == NULL &&
        upipe_h265f->flow_def_attr != NULL)
        return false;

    if (upipe_h265f->flow_def_input == NULL) {
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        uref_free(uref);
        return true;
    }

    const uint8_t *p;
    size_t size;
    if (unlikely(ubase_check(uref_flow_get_headers(uref, &p, &size)))) {
        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_h265f->ubuf_mgr,
                                                         p, size);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return true;
        }
        uref_flow_delete_headers(uref);
        uref_attach_ubuf(uref, ubuf);
    }

    switch (upipe_h265f->encaps_input) {
        case UREF_H26X_ENCAPS_NALU:
            return upipe_h265f_work_nalu(upipe, uref, upump_p);
        case UREF_H26X_ENCAPS_LENGTH1:
        case UREF_H26X_ENCAPS_LENGTH2:
        case UREF_H26X_ENCAPS_LENGTH4:
            return upipe_h265f_work_length(upipe, uref, upump_p);
        case UREF_H26X_ENCAPS_ANNEXB:
            break;
        default:
            upipe_err(upipe, "global headers not found");
            uref_free(uref);
            return true;
    }

    if (unlikely(ubase_check(uref_flow_get_discontinuity(uref))))
        upipe_h265f->got_discontinuity = true;

    upipe_h265f_append_uref_stream(upipe, uref);
    upipe_h265f_work_annexb(upipe, upump_p);
    return true;
}

/** @internal @This inputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_h265f_input(struct upipe *upipe, struct uref *uref,
                              struct upump **upump_p)
{
    if (!upipe_h265f_check_input(upipe)) {
        upipe_h265f_hold_input(upipe, uref);
        upipe_h265f_block_input(upipe, upump_p);
    } else if (!upipe_h265f_handle(upipe, uref, upump_p)) {
        upipe_h265f_hold_input(upipe, uref);
        upipe_h265f_block_input(upipe, upump_p);
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
static int upipe_h265f_check_flow_format(struct upipe *upipe,
                                         struct uref *flow_format)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    upipe_h265f_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This receives the result of a ubuf manager request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_h265f_check_ubuf_mgr(struct upipe *upipe,
                                      struct uref *flow_format)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;
    if (upipe_h265f->flow_def_attr == NULL) {
        /* temporary ubuf manager, will be overwritten later */
        uref_free(flow_format);
        return UBASE_ERR_NONE;
    }

    uref_free(upipe_h265f->flow_def_requested);
    upipe_h265f->flow_def_requested = flow_format;
    upipe_h265f->encaps_output = uref_h26x_flow_infer_encaps(flow_format);
    ubuf_free(upipe_h265f->annexb_header);
    upipe_h265f->annexb_header =
        upipe_h26xf_alloc_annexb(upipe_h265f->ubuf_mgr);
    UBASE_ALLOC_RETURN(upipe_h265f->annexb_header);

    ubuf_free(upipe_h265f->annexb_aud);
    uint8_t aud_buffer[6] = { 0, 0, 0, 1, 0, 0 };
    h265nal_set_type(aud_buffer + 1, H265NAL_TYPE_AUD);
    upipe_h265f->annexb_aud =
        ubuf_block_alloc_from_opaque(upipe_h265f->ubuf_mgr, aud_buffer, 6);
    UBASE_ALLOC_RETURN(upipe_h265f->annexb_aud);

    upipe_h265f_build_flow_def(upipe);

    if (upipe_h265f->uref_output) {
        upipe_h265f_output_au(upipe, upipe_h265f->uref_output, NULL);
        upipe_h265f->uref_output = NULL;
    }

    bool was_buffered = !upipe_h265f_check_input(upipe);
    upipe_h265f_output_input(upipe);
    upipe_h265f_unblock_input(upipe);
    if (was_buffered && upipe_h265f_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_h265f_input. */
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
static int upipe_h265f_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    if (unlikely(!ubase_check(uref_flow_get_def(flow_def, &def)) ||
                 (ubase_ncmp(def, "block.hevc.") && strcmp(def, "block."))))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (upipe_h265f->ubuf_mgr == NULL) {
        /* We have to get a ubuf manager to parse the global headers. */
        upipe_h265f_demand_ubuf_mgr(upipe, flow_def_dup);

        if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    }
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a h265f pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_h265f_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_h265f_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_h265f_set_flow_def(upipe, flow_def);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_h265f_free(struct upipe *upipe)
{
    struct upipe_h265f *upipe_h265f = upipe_h265f_from_upipe(upipe);

    /* Output any buffered frame. */
    if (upipe_h265f->encaps_input == UREF_H26X_ENCAPS_ANNEXB &&
        !upipe_h265f->complete_input && upipe_h265f->au_size) {
        upipe_h265f_end_annexb(upipe, NULL);
        struct uref *uref = upipe_h265f_prepare_annexb(upipe);
        if (uref != NULL)
            upipe_h265f_output_au(upipe, uref, NULL);
    }

    upipe_throw_dead(upipe);

    upipe_h265f_clean_uref_stream(upipe);
    upipe_h265f_clean_input(upipe);
    upipe_h265f_clean_output(upipe);
    uref_free(upipe_h265f->flow_def_requested);
    uref_free(upipe_h265f->uref_output);
    ubuf_free(upipe_h265f->annexb_header);
    ubuf_free(upipe_h265f->annexb_aud);
    upipe_h265f_clean_flow_format(upipe);
    upipe_h265f_clean_flow_def(upipe);
    upipe_h265f_clean_ubuf_mgr(upipe);
    upipe_h265f_clean_sync(upipe);

    int i;
    for (i = 0; i < H265VPS_ID_MAX; i++)
        if (upipe_h265f->vps[i] != NULL)
            ubuf_free(upipe_h265f->vps[i]);

    for (i = 0; i < H265SPS_ID_MAX; i++)
        if (upipe_h265f->sps[i] != NULL)
            ubuf_free(upipe_h265f->sps[i]);

    for (i = 0; i < H265PPS_ID_MAX; i++)
        if (upipe_h265f->pps[i] != NULL)
            ubuf_free(upipe_h265f->pps[i]);

    upipe_h265f_clean_urefcount(upipe);
    upipe_h265f_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_h265f_mgr = {
    .refcount = NULL,
    .signature = UPIPE_H265F_SIGNATURE,

    .upipe_alloc = upipe_h265f_alloc,
    .upipe_input = upipe_h265f_input,
    .upipe_control = upipe_h265f_control,

    .upipe_mgr_control = NULL

};

/** @This returns the management structure for all h265f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_h265f_mgr_alloc(void)
{
    return &upipe_h265f_mgr;
}

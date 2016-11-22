/*
 * RFC 4175 unpacking
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** @file
 * @short Upipe unpack rfc 4175 module
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/upipe.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf_block.h>
#include <upipe/uref_block.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_flow.h>

#include <upipe-hbrmt/upipe_unpack_rfc4175.h>
#include "sdidec.h"

#include <bitstream/ietf/rfc4175.h>
#include <bitstream/ietf/rtp.h>

#define UPIPE_UNPACK_RFC4175_MAX_PLANES 3

/** upipe_unpack_rfc4175 structure */
struct upipe_unpack_rfc4175 {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** Set from reading the input flow def */
    bool output_is_v210;
    int output_bit_depth;

    /* RTP packet stuff */
    bool discontinuity;
    uint64_t expected_seqnum;

    /** output chroma map */
    const char *output_chroma_map[UPIPE_UNPACK_RFC4175_MAX_PLANES];

    struct ubuf *ubuf;
    uint8_t *output_plane[UPIPE_UNPACK_RFC4175_MAX_PLANES];
    size_t output_stride[UPIPE_UNPACK_RFC4175_MAX_PLANES];

    /** indicates next packet is a start of a frame */
    bool next_packet_frame_start;

    /** Bitpacked to V210 conversion */
    void (*bitpacked_to_v210)(const uint8_t *src, uint32_t *dst, int64_t size);

    /** Bitpacked to Planar 8 conversion */
    void (*bitpacked_to_planar_8)(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t size);

    /** Bitpacked to Planar 10 conversion */
    void (*bitpacked_to_uyvy)(const uint8_t *src, uint16_t *y, int64_t size);

    /** last RTP timestamp */
    uint64_t last_rtp_timestamp;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static bool upipe_unpack_rfc4175_handle(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);
/** @hidden */
static int upipe_unpack_rfc4175_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_unpack_rfc4175, upipe, UPIPE_UNPACK_RFC4175_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_unpack_rfc4175, urefcount, upipe_unpack_rfc4175_free);
UPIPE_HELPER_VOID(upipe_unpack_rfc4175);
UPIPE_HELPER_FLOW(upipe_unpack_rfc4175, NULL);
UPIPE_HELPER_OUTPUT(upipe_unpack_rfc4175, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_unpack_rfc4175, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_unpack_rfc4175_check,
                      upipe_unpack_rfc4175_register_output_request,
                      upipe_unpack_rfc4175_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_unpack_rfc4175, urefs, nb_urefs, max_urefs, blockers, upipe_unpack_rfc4175_handle)

/* One indexed separated IN, Zero indexed interleaved OUT */
static inline int get_interleaved_line(int line_number)
{
    assert(line_number <= 1080);
    if (line_number > 540){
        return (line_number - 540) * 2 - 1;
    } else {
        return (line_number - 1) * 2;
    }
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_unpack_rfc4175_handle(struct upipe *upipe, struct uref *uref,
                                        struct upump **upump_p)
{
    struct upipe_unpack_rfc4175 *upipe_unpack_rfc4175 = upipe_unpack_rfc4175_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_unpack_rfc4175_store_flow_def(upipe, NULL);
        upipe_unpack_rfc4175_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_unpack_rfc4175->flow_def == NULL)
        return false;

    /* map input */
    int input_size = -1;
    const uint8_t *input_buf = NULL;
    if (unlikely(uref_block_read(uref, 0, &input_size, &input_buf))) {
        upipe_warn(upipe, "unable to map input");
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return true;
    }

    uint8_t marker = rtp_check_marker(input_buf);
    uint16_t seqnum = rtp_get_seqnum(input_buf);

    if (unlikely(upipe_unpack_rfc4175->expected_seqnum != -1 &&
                 seqnum != upipe_unpack_rfc4175->expected_seqnum)) {
        upipe_warn_va(upipe, "potentially lost %d RTP packets, got %u expected %u",
                      (seqnum + UINT16_MAX + 1 - upipe_unpack_rfc4175->expected_seqnum) &
                      UINT16_MAX, seqnum, upipe_unpack_rfc4175->expected_seqnum);
        upipe_unpack_rfc4175->discontinuity = 1;
    }
    upipe_unpack_rfc4175->expected_seqnum = (seqnum + 1) & UINT16_MAX;

    if (upipe_unpack_rfc4175->next_packet_frame_start) {
        /* FIXME: w/h */
        const size_t output_hsize = 1920, output_vsize = 1080;
        /* FIXME: this is for v210 only */
        size_t aligned_output_hsize = ((output_hsize + 47) / 48) * 48;
        upipe_unpack_rfc4175->ubuf = ubuf_pic_alloc(upipe_unpack_rfc4175->ubuf_mgr,
                                                    aligned_output_hsize, output_vsize);
        if (unlikely(upipe_unpack_rfc4175->ubuf == NULL)) {
            upipe_warn(upipe, "unable to allocate output");
            uref_block_unmap(uref, 0);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }

        /* map output */
        for (int i = 0; i < UPIPE_UNPACK_RFC4175_MAX_PLANES ; i++) {
            const char *chroma = upipe_unpack_rfc4175->output_chroma_map[i];
            if (chroma == NULL)
                break;

            if (unlikely(!ubase_check(ubuf_pic_plane_write(upipe_unpack_rfc4175->ubuf,
                                chroma, 0, 0, -1, -1,
                                &upipe_unpack_rfc4175->output_plane[i])) ||
                         !ubase_check(ubuf_pic_plane_size(upipe_unpack_rfc4175->ubuf,
                                 chroma,
                                 &upipe_unpack_rfc4175->output_stride[i],
                                 NULL, NULL, NULL)))) {
                upipe_warn(upipe, "unable to map output");
                ubuf_free(upipe_unpack_rfc4175->ubuf);
                uref_free(uref);
                return true;
            }
        }
    }

    const uint8_t *rfc4175_data = &input_buf[RTP_HEADER_SIZE + RFC_4175_EXT_SEQ_NUM_LEN];

    uint16_t length[2], field[2], line_number[2], line_offset[2];

    uint8_t continuation = rfc4175_get_line_continuation(rfc4175_data);
    for (int i = 0; i < 1 + !!continuation; i++) {
        length[i]      = rfc4175_get_line_length(rfc4175_data);
        field[i]       = rfc4175_get_line_field_id(rfc4175_data);
        line_number[i] = rfc4175_get_line_number(rfc4175_data);
        line_offset[i] = rfc4175_get_line_offset(rfc4175_data);
        rfc4175_data += RFC_4175_HEADER_LEN;
    }

    // FIXME sanity check all of these

    upipe_unpack_rfc4175->next_packet_frame_start = marker && field[0];

    if (!upipe_unpack_rfc4175->ubuf)
        goto end;

    for (int i = 0; i < 1 + !!continuation; i++) {
        int interleaved_line = get_interleaved_line(line_number[i]);

        if (upipe_unpack_rfc4175->output_is_v210) {
            /* Start */
            uint8_t *dst = upipe_unpack_rfc4175->output_plane[0] +
                upipe_unpack_rfc4175->output_stride[0] * interleaved_line;

            /* Offset to a pixel/pblock within the line */
            dst += (line_offset[i] / 6) * 16;

            upipe_unpack_rfc4175->bitpacked_to_v210(rfc4175_data,
                    (uint32_t *)dst, length[i]);
        } else {
            uint8_t *plane[UPIPE_UNPACK_RFC4175_MAX_PLANES];
            for (int j = 0 ; j < UPIPE_UNPACK_RFC4175_MAX_PLANES; j++) {
                plane[j] = upipe_unpack_rfc4175->output_plane[j] +
                    upipe_unpack_rfc4175->output_stride[j] * interleaved_line +
                    line_offset[i] / (j ? 2 : 1); // XXX: hsub
            }

            upipe_unpack_rfc4175->bitpacked_to_planar_8(rfc4175_data,
                    plane[0], plane[1], plane[2], length[i]);
        }
        rfc4175_data += length[0];
    }

    if (!upipe_unpack_rfc4175->next_packet_frame_start)
        goto end;

    /* unmap output */
    for (int i = 0; i < UPIPE_UNPACK_RFC4175_MAX_PLANES; i++) {
        const char *chroma = upipe_unpack_rfc4175->output_chroma_map[i];
        if (chroma == NULL)
            break;
        ubuf_pic_plane_unmap(upipe_unpack_rfc4175->ubuf,
                chroma, 0, 0, -1, -1);
    }

    uint32_t timestamp = rtp_get_timestamp(input_buf);
    uref_block_unmap(uref, 0);

    uint64_t delta =
        (UINT32_MAX + timestamp -
         (upipe_unpack_rfc4175->last_rtp_timestamp % UINT32_MAX)) % UINT32_MAX;
    upipe_unpack_rfc4175->last_rtp_timestamp += delta;

    uint64_t pts = upipe_unpack_rfc4175->last_rtp_timestamp;
    pts = pts * UCLOCK_FREQ / 90000;

    uref_clock_set_pts_prog(uref, pts);
    uref_clock_set_pts_orig(uref, timestamp * UCLOCK_FREQ / 90000);
    uref_clock_set_dts_pts_delay(uref, 0);

    upipe_throw_clock_ref(upipe, uref, pts, 0);
    upipe_throw_clock_ts(upipe, uref);

    uref_attach_ubuf(uref, upipe_unpack_rfc4175->ubuf);
    upipe_unpack_rfc4175_output(upipe, uref, upump_p);
    upipe_unpack_rfc4175->ubuf = NULL;
    return true;

end:
    /* unmap input */
    uref_block_unmap(uref, 0);
    uref_free(uref);

    return true;
}

/** @internal @This receives incoming uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the picture
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_unpack_rfc4175_input(struct upipe *upipe, struct uref *uref,
                                       struct upump **upump_p)
{
    if (!upipe_unpack_rfc4175_check_input(upipe)) {
        upipe_unpack_rfc4175_hold_input(upipe, uref);
        upipe_unpack_rfc4175_block_input(upipe, upump_p);
    } else if (!upipe_unpack_rfc4175_handle(upipe, uref, upump_p)) {
        upipe_unpack_rfc4175_hold_input(upipe, uref);
        upipe_unpack_rfc4175_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_unpack_rfc4175_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_unpack_rfc4175 *upipe_unpack_rfc4175 = upipe_unpack_rfc4175_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_unpack_rfc4175_store_flow_def(upipe, flow_format);

    if (upipe_unpack_rfc4175->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_unpack_rfc4175_check_input(upipe);
    upipe_unpack_rfc4175_output_input(upipe);
    upipe_unpack_rfc4175_unblock_input(upipe);
    if (was_buffered && upipe_unpack_rfc4175_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_unpack_rfc4175_input. */
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
static int upipe_unpack_rfc4175_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_unpack_rfc4175 *upipe_unpack_rfc4175 = upipe_unpack_rfc4175_from_upipe(upipe);

    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))

    struct uref *flow_def_dup;

    if ((flow_def_dup = uref_sibling_alloc(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;

    uref_flow_set_def(flow_def_dup, "pic.");

    if (upipe_unpack_rfc4175->output_is_v210) {
        upipe_unpack_rfc4175->output_chroma_map[0] = "u10y10v10y10u10y10v10y10u10y10v10y10";
        upipe_unpack_rfc4175->output_chroma_map[1] = NULL;
        upipe_unpack_rfc4175->output_chroma_map[2] = NULL;
        uref_pic_flow_set_align(flow_def_dup, 16);
        uref_pic_flow_set_planes(flow_def_dup, 1);
        uref_pic_flow_set_macropixel(flow_def_dup, 48);
        uref_pic_flow_set_macropixel_size(flow_def_dup, 128, 0);
        uref_pic_flow_set_chroma(flow_def_dup, upipe_unpack_rfc4175->output_chroma_map[0], 0);
    } else if (upipe_unpack_rfc4175->output_bit_depth == 8) {
        upipe_unpack_rfc4175->output_chroma_map[0] = "y8";
        upipe_unpack_rfc4175->output_chroma_map[1] = "u8";
        upipe_unpack_rfc4175->output_chroma_map[2] = "v8";
        UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def_dup, 1))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 1, 1, 1, "y8"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 1, "u8"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 1, "v8"))
    } else {
        upipe_unpack_rfc4175->output_chroma_map[0] = "y10l";
        upipe_unpack_rfc4175->output_chroma_map[1] = "u10l";
        upipe_unpack_rfc4175->output_chroma_map[2] = "v10l";
        UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def_dup, 1))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 1, 1, 2, "y10l"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 2, "u10l"))
        UBASE_RETURN(uref_pic_flow_add_plane(flow_def_dup, 2, 1, 2, "v10l"))
    }

    struct urational fps = { .num = 30000, .den = 1001 };
    uref_pic_flow_set_fps(flow_def_dup, fps);

    uref_pic_flow_set_hsize(flow_def_dup, 1920);
    uref_pic_flow_set_vsize(flow_def_dup, 1080);

    uref_pic_flow_set_hsubsampling(flow_def_dup, 1, 0);
    uref_pic_flow_set_vsubsampling(flow_def_dup, 1, 0);

    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_unpack_rfc4175_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return upipe_throw_provide_request(upipe, request);
            return upipe_unpack_rfc4175_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UBUF_MGR ||
                request->type == UREQUEST_FLOW_FORMAT)
                return UBASE_ERR_NONE;
            return upipe_unpack_rfc4175_free_output_proxy(upipe, request);
        }

        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_unpack_rfc4175_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_unpack_rfc4175_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_unpack_rfc4175_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_unpack_rfc4175_set_flow_def(upipe, flow);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a upipe_unpack_rfc4175 pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_unpack_rfc4175_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_unpack_rfc4175_alloc_flow(mgr, uprobe, signature,
                                                          args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_unpack_rfc4175 *upipe_unpack_rfc4175 = upipe_unpack_rfc4175_from_upipe(upipe);

    upipe_unpack_rfc4175->output_is_v210 = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 128, "u10y10v10y10u10y10v10y10u10y10v10y10"));
    if (!upipe_unpack_rfc4175->output_is_v210)
         upipe_unpack_rfc4175->output_bit_depth = ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ? 8 : 10;

    upipe_unpack_rfc4175->bitpacked_to_uyvy = upipe_sdi_unpack_c;
    upipe_unpack_rfc4175->bitpacked_to_v210 = upipe_sdi_v210_unpack_c;
    upipe_unpack_rfc4175->bitpacked_to_planar_8 = upipe_sdi_to_planar_8_c;

#if !defined(__APPLE__) /* macOS clang doesn't support that builtin yet */
#if defined(__clang__) && /* clang 3.8 doesn't know ssse3 */ \
     (__clang_major__ < 3 || (__clang_major__ == 3 && __clang_minor__ <= 8))
# ifdef __SSSE3__
    if (1)
# else
    if (0)
# endif
#else
    if (__builtin_cpu_supports("ssse3"))
#endif
        upipe_unpack_rfc4175->bitpacked_to_uyvy = upipe_sdi_unpack_10_ssse3;

   if (__builtin_cpu_supports("avx")) {
        upipe_unpack_rfc4175->bitpacked_to_v210 = upipe_sdi_v210_unpack_avx;
        upipe_unpack_rfc4175->bitpacked_to_planar_8 = upipe_sdi_to_planar_8_avx;
    }
#endif

    upipe_unpack_rfc4175_init_urefcount(upipe);
    upipe_unpack_rfc4175_init_ubuf_mgr(upipe);
    upipe_unpack_rfc4175_init_output(upipe);
    upipe_unpack_rfc4175_init_input(upipe);

    upipe_unpack_rfc4175->expected_seqnum = -1;
    upipe_unpack_rfc4175->last_rtp_timestamp = UINT32_MAX;
    upipe_unpack_rfc4175->ubuf = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_unpack_rfc4175_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_unpack_rfc4175_clean_input(upipe);
    upipe_unpack_rfc4175_clean_output(upipe);
    upipe_unpack_rfc4175_clean_ubuf_mgr(upipe);
    upipe_unpack_rfc4175_clean_urefcount(upipe);
    upipe_unpack_rfc4175_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_unpack_rfc4175_mgr = {
    .refcount = NULL,
    .signature = UPIPE_UNPACK_RFC4175_SIGNATURE,

    .upipe_alloc = upipe_unpack_rfc4175_alloc,
    .upipe_input = upipe_unpack_rfc4175_input,
    .upipe_control = upipe_unpack_rfc4175_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for upipe_unpack_rfc4175 pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_unpack_rfc4175_mgr_alloc(void)
{
    return &upipe_unpack_rfc4175_mgr;
}

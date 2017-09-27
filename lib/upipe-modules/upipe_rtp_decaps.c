/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe module decapsulating RTP header from blocks
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe-modules/upipe_rtp_decaps.h>
#include <upipe-framers/uref_mpga_flow.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-framers/uref_h26x.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/ietf/rtp3551.h>
#include <bitstream/ietf/rtp2250.h>
#include <bitstream/ietf/rtp3640.h>
#include <bitstream/ietf/rtp6184.h>
#include <bitstream/mpeg/h264.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** RTP timestamps wrap at 32 bits */
#define POW2_32 UINT64_C(4294967296)

/** @This is a list of supported outputs. */
enum upipe_rtpd_mode {
    /** PCM mu-law */
    UPIPE_RTPD_PCMU = 0,
    /** GSM */
    UPIPE_RTPD_GSM,
    /** PCM a-law */
    UPIPE_RTPD_PCMA,
    /** PCM */
    UPIPE_RTPD_PCM,
    /** QCELP */
    UPIPE_RTPD_QCELP,
    /** MPEG audio */
    UPIPE_RTPD_MPA,
    /** MPEG video */
    UPIPE_RTPD_MPV,
    /** MPEG transport stream */
    UPIPE_RTPD_MP2T,
    /** Opus */
    UPIPE_RTPD_OPUS,
    /** ITU-T H.264 */
    UPIPE_RTPD_H264,
    /** ISO/IEC 14496-3 (RFC3640) */
    UPIPE_RTPD_MPEG4_AUDIO,
    /** Unknown */
    UPIPE_RTPD_UNKNOWN
};

/** @This is a list of input flow definitions matching the supported
 * outputs. */
static const char *upipe_rtpd_flow_defs[] = {
    "block.rtp.pcm_mulaw.sound",
    "block.rtp.gsm.sound",
    "block.rtp.pcm_alaw.sound",
    "block.rtp.sound.",
    "block.rtp.qcelp.sound",
    "block.rtp.mp3.sound",
    "block.rtp.mpeg2video.pic",
    "block.rtp.mpegtsaligned.",
    "block.rtp.opus.sound.",
    "block.rtp.h264.pic.",
    "block.rtp.aac.sound.",
    "block.rtp.",
    NULL
};

/** upipe_rtpd structure */
struct upipe_rtpd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** expected sequence number */
    int expected_seqnum;
    /** current RTP type */
    uint8_t type;
    /** current output mode */
    enum upipe_rtpd_mode mode;
    /** configured output mode */
    enum upipe_rtpd_mode mode_config;
    /** configured sample rate */
    uint64_t rate;
    /** last timestamp */
    uint64_t last_timestamp;
    /** next uref (for H.264 and MPEG-4 audio) */
    struct uref *next_uref;
    /** next uref size */
    size_t next_uref_size;
    /** next uref NAL (for H.264) */
    uint64_t next_uref_nal;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** output pipe */
    struct upipe *output;
    /** input flow definition packet */
    struct uref *flow_def_input;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /* number of packets lost */
    uint64_t lost;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_rtpd_check_ubuf_mgr(struct upipe *upipe,
                                     struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_rtpd, upipe, UPIPE_RTPD_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtpd, urefcount, upipe_rtpd_free);
UPIPE_HELPER_VOID(upipe_rtpd);
UPIPE_HELPER_OUTPUT(upipe_rtpd, output, flow_def, output_state, request_list);
UPIPE_HELPER_UBUF_MGR(upipe_rtpd, ubuf_mgr, flow_format,
                      ubuf_mgr_request, upipe_rtpd_check_ubuf_mgr,
                      upipe_rtpd_register_output_request,
                      upipe_rtpd_unregister_output_request)

/** @internal @This allocates a rtpd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_rtpd_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_rtpd_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    upipe_rtpd_init_urefcount(upipe);
    upipe_rtpd_init_output(upipe);
    upipe_rtpd_init_ubuf_mgr(upipe);
    upipe_rtpd->expected_seqnum = -1;
    upipe_rtpd->type = UINT8_MAX;
    upipe_rtpd->mode = upipe_rtpd->mode_config = UPIPE_RTPD_UNKNOWN;
    upipe_rtpd->lost = 0;
    upipe_rtpd->flow_def_input = NULL;
    upipe_rtpd->rate = 0;
    upipe_rtpd->last_timestamp = UINT64_MAX;
    upipe_rtpd->next_uref = NULL;
    upipe_rtpd->next_uref_size = 0;
    upipe_rtpd->next_uref_nal = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This builds the output flow definition and sets the mode.
 *
 * @param upipe description structure of the pipe
 */
static inline void upipe_rtpd_build_flow_def(struct upipe *upipe)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    assert(upipe_rtpd->flow_def_input != NULL);
    struct uref *flow_def = uref_dup(upipe_rtpd->flow_def_input);
    if (unlikely(flow_def == NULL)) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    /* infer mode from type */
    upipe_rtpd->mode = UPIPE_RTPD_UNKNOWN;
    const char *def = "block.";
    uint64_t rate = 90000;
    uint8_t channels = 0;
    switch (upipe_rtpd->type) {
        case RTP_TYPE_PCMU:
            upipe_rtpd->mode = UPIPE_RTPD_PCMU;
            rate = 8000;
            def = "block.pcm_mulaw.sound.";
            break;
        case RTP_TYPE_GSM:
            upipe_rtpd->mode = UPIPE_RTPD_GSM;
            rate = 8000;
            def = "block.gsm.sound.";
            break;
        case RTP_TYPE_PCMA:
            upipe_rtpd->mode = UPIPE_RTPD_PCMA;
            rate = 8000;
            def = "block.pcm_alaw.sound.";
            break;
        case RTP_TYPE_L16:
            upipe_rtpd->mode = UPIPE_RTPD_PCM;
            rate = 44100;
            channels = 2;
            def = "block.sound.s16be.";
            break;
        case RTP_TYPE_L16MONO:
            upipe_rtpd->mode = UPIPE_RTPD_PCM;
            rate = 44100;
            channels = 1;
            def = "block.sound.s16be.";
            break;
        case RTP_TYPE_QCELP:
            upipe_rtpd->mode = UPIPE_RTPD_QCELP;
            rate = 8000;
            def = "block.qcelp.sound";
            break;
        case RTP_TYPE_MPA:
            upipe_rtpd->mode = UPIPE_RTPD_MPA;
            def = "block.mp3.sound.";
            break;
        case RTP_TYPE_MPV:
            upipe_rtpd->mode = UPIPE_RTPD_MPV;
            def = "block.mpeg2video.pic.";
            break;
        case RTP_TYPE_MP2T:
            upipe_rtpd->mode = UPIPE_RTPD_MP2T;
            def = "block.mpegtsaligned.";
            break;
        default:
            break;
    }

    if (upipe_rtpd->mode == UPIPE_RTPD_UNKNOWN) {
        uref_flow_get_def(upipe_rtpd->flow_def_input, &def);

        upipe_rtpd->mode = upipe_rtpd->mode_config;
        if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def, "block.%s",
                            def + strlen("block.rtp.")))))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    } else {
        if (upipe_rtpd->mode_config != UPIPE_RTPD_UNKNOWN &&
            upipe_rtpd->mode != upipe_rtpd->mode_config)
            upipe_warn_va(upipe,
                          "flow def %s is not compatible with RTP type %"PRIu8,
                          def, upipe_rtpd->type);
        uref_flow_set_def(flow_def, def);
    }
    uref_sound_flow_get_rate(upipe_rtpd->flow_def_input, &rate);
    uref_sound_flow_get_channels(upipe_rtpd->flow_def_input, &channels);

    switch (upipe_rtpd->mode) {
        case UPIPE_RTPD_H264:
            uref_h26x_flow_set_encaps(flow_def, UREF_H26X_ENCAPS_NALU);
            uref_flow_set_complete(flow_def);
            break;
        case UPIPE_RTPD_MPEG4_AUDIO:
            uref_mpga_flow_set_encaps(flow_def, UREF_MPGA_ENCAPS_RAW);
            uref_flow_set_complete(flow_def);
            break;
        case UPIPE_RTPD_MPA:
        case UPIPE_RTPD_MPV:
        case UPIPE_RTPD_MP2T:
            break;
        case UPIPE_RTPD_PCM:
            if (channels)
                uref_sound_flow_set_channels(flow_def, channels);
            /* fallthrough */
        case UPIPE_RTPD_UNKNOWN:
        case UPIPE_RTPD_PCMA:
        case UPIPE_RTPD_PCMU:
        case UPIPE_RTPD_GSM:
        case UPIPE_RTPD_QCELP:
            if (upipe_rtpd->rate) {
                rate = upipe_rtpd->rate;
                uref_sound_flow_set_rate(flow_def, upipe_rtpd->rate);
            }
            break;
        case UPIPE_RTPD_OPUS:
            rate = 48000;
            break;
    }
    upipe_rtpd->rate = rate;
    uref_clock_set_wrap(flow_def, POW2_32 * UCLOCK_FREQ / rate);

    upipe_rtpd_store_flow_def(upipe, flow_def);
}

/** @internal @This outputs MPEG-1/2 audio data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_rtpd_output_mpa(struct upipe *upipe, struct uref *uref,
                                         struct upump **upump_p)
{
    uref_block_resize(uref, RTP2250A_HEADER_SIZE, -1);
    upipe_rtpd_output(upipe, uref, upump_p);
}

/** @internal @This outputs MPEG-1/2 video data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_rtpd_output_mpv(struct upipe *upipe, struct uref *uref,
                                         struct upump **upump_p)
{
    uint8_t rtp_buffer[RTP2250V_HEADER_SIZE];
    const uint8_t *rtp_header = uref_block_peek(uref, 0, RTP2250V_HEADER_SIZE,
                                                rtp_buffer);
    if (unlikely(rtp_header == NULL)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }
    size_t offset = RTP2250V_HEADER_SIZE +
        rtp2250v_check_mpeg2(rtp_header) * RTP2250VX_HEADER_SIZE;
    uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);
    uref_block_resize(uref, offset, -1);
    upipe_rtpd_output(upipe, uref, upump_p);
}

/** @internal @This appends a NAL of H.264 video data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static inline void upipe_rtpd_append_h264_nal(struct upipe *upipe,
                                              struct uref *uref)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    size_t size = 0;
    uref_block_size(uref, &size);
    if (upipe_rtpd->next_uref == NULL) {
        upipe_rtpd->next_uref = uref;
        upipe_rtpd->next_uref_size = size;
        return;
    }

    uref_h26x_set_nal_offset(upipe_rtpd->next_uref, upipe_rtpd->next_uref_size,
                             upipe_rtpd->next_uref_nal++);
    uref_block_append(upipe_rtpd->next_uref, uref_detach_ubuf(uref));
    upipe_rtpd->next_uref_size += size;
    uref_free(uref);
}

/** @internal @This outputs H.264 video data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_rtpd_output_h264(struct upipe *upipe,
                                          struct uref *uref,
                                          struct upump **upump_p)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    if (unlikely(upipe_rtpd->ubuf_mgr == NULL)) {
        upipe_err(upipe, "no ubuf manager received");
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    if (upipe_rtpd->next_uref != NULL) {
        uint64_t pts;
        /* New PTS means new access unit */
        if (ubase_check(uref_clock_get_pts_orig(uref, &pts))) {
            upipe_rtpd_output(upipe, upipe_rtpd->next_uref, upump_p);
            upipe_rtpd->next_uref = NULL;
            upipe_rtpd->next_uref_size = 0;
            upipe_rtpd->next_uref_nal = 0;
        }
    }

    uint8_t nal_header;
    if (unlikely(!ubase_check(uref_block_extract(uref, 0, 1, &nal_header)))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }
    uint8_t nal_type = h264nalst_get_type(nal_header);
    if (nal_type == RTP_6184_STAP_B || nal_type == RTP_6184_MTAP16 ||
        nal_type == RTP_6184_MTAP24 || nal_type == RTP_6184_FU_B) {
        upipe_err(upipe, "H264 interleaving is not supported");
        uref_free(uref);
        return;
    }

    if (nal_type < RTP_6184_STAP_A) {
        /* Single NAL Unit Packet */
        upipe_rtpd_append_h264_nal(upipe, uref);
        return;
    }

    if (nal_type == RTP_6184_FU_A) {
        uint8_t fu_header;
        if (unlikely(!ubase_check(uref_block_extract(uref, 1, 1,
                                                     &fu_header)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }
        uref_block_resize(uref, 2, -1);

        if (!rtp_6184_fu_check_start(fu_header)) {
            if (upipe_rtpd->next_uref == NULL) {
                upipe_warn(upipe, "discarding incomplete fragmented NAL");
                uref_free(uref);
                return;
            }

            size_t size = 0;
            uref_block_size(uref, &size);
            uref_block_append(upipe_rtpd->next_uref, uref_detach_ubuf(uref));
            upipe_rtpd->next_uref_size += size;
            /* Do not increment next_uref_nal because it is not a new NAL */
            uref_free(uref);
            return;
        }

        h264nalst_set_type(&nal_header, h264nalst_get_type(fu_header));
        struct ubuf *ubuf = ubuf_block_alloc_from_opaque(upipe_rtpd->ubuf_mgr,
                                                         &nal_header, 1);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        ubuf_block_append(ubuf, uref_detach_ubuf(uref));
        uref_attach_ubuf(uref, ubuf);
        upipe_rtpd_append_h264_nal(upipe, uref);
        return;
    }

    if (nal_type != RTP_6184_STAP_A) {
        upipe_warn_va(upipe, "unknown NAL type %"PRIu8, nal_type);
        uref_free(uref);
        return;
    }

    uref_block_resize(uref, 1, -1);
    size_t size;
    while (ubase_check(uref_block_size(uref, &size)) && size) {
        uint8_t size_header[2];
        if (unlikely(!ubase_check(uref_block_extract(uref, 0, 2,
                                                     size_header)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }
        uref_block_resize(uref, 2, -1);
        uint16_t nal_size = rtp_6184_stap_get_size(size_header);

        struct uref *dup = uref_dup(uref);
        if (unlikely(dup == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        uref_block_truncate(dup, nal_size);
        uref_block_resize(uref, nal_size, -1);
        upipe_rtpd_append_h264_nal(upipe, dup);
    }
    uref_free(uref);
}

/** @internal @This outputs MPEG-4 audio data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @param marker marker bit in the RTP header
 */
static inline void upipe_rtpd_output_mpeg4_audio(struct upipe *upipe,
                                                 struct uref *uref,
                                                 struct upump **upump_p,
                                                 bool marker)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    uint8_t au_headers[RTP3640_AU_HEADERS_LENGTH_SIZE];
    if (unlikely(!ubase_check(uref_block_extract(uref, 0,
                        RTP3640_AU_HEADERS_LENGTH_SIZE, au_headers)))) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }
    uint16_t au_headers_length = rtp3640_get_au_headers_length(au_headers);
    /* convert to octets */
    au_headers_length /= 8;

    if (au_headers_length == RTP3640_AU_HEADER_AAC_HBR_SIZE) {
        /* one frame or fragment, ignore headers */
        uref_block_resize(uref, RTP3640_AU_HEADERS_LENGTH_SIZE +
                                RTP3640_AU_HEADER_AAC_HBR_SIZE, -1);

        /* append to next uref */
        if (upipe_rtpd->next_uref == NULL)
            upipe_rtpd->next_uref = uref;
        else {
            uref_block_append(upipe_rtpd->next_uref, uref_detach_ubuf(uref));
            uref_free(uref);
        }

        if (marker) {
            upipe_rtpd_output(upipe, upipe_rtpd->next_uref, upump_p);
            upipe_rtpd->next_uref = NULL;
        }
        return;
    }

    /* concatenated frames */
    if (upipe_rtpd->next_uref != NULL) {
        upipe_dbg(upipe, "outputting a fragment");
        upipe_rtpd_output(upipe, upipe_rtpd->next_uref, upump_p);
        upipe_rtpd->next_uref = NULL;
    }

    uint16_t i;
    size_t offset = au_headers_length;
    for (i = 0; i < au_headers_length / RTP3640_AU_HEADER_AAC_HBR_SIZE; i++) {
        uint8_t au_header[RTP3640_AU_HEADER_AAC_HBR_SIZE];
        if (unlikely(!ubase_check(uref_block_extract(uref,
                            RTP3640_AU_HEADERS_LENGTH_SIZE +
                            RTP3640_AU_HEADER_AAC_HBR_SIZE * i,
                            RTP3640_AU_HEADER_AAC_HBR_SIZE, au_header)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }

        uint16_t au_size = rtp3640_get_aac_hbr_au_size(au_header);
        struct uref *frame = uref_block_splice(uref, offset, au_size);
        if (unlikely(frame == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }
        /* TODO support interleaving */
        upipe_rtpd_output(upipe, frame, upump_p);
        offset += au_size;
    }
    uref_free(uref);
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static inline void upipe_rtpd_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    uint8_t rtp_buffer[RTP_HEADER_SIZE];
    const uint8_t *rtp_header = uref_block_peek(uref, 0, RTP_HEADER_SIZE,
                                                rtp_buffer);
    if (unlikely(rtp_header == NULL)) {
        upipe_warn(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    bool valid = rtp_check_hdr(rtp_header);
    bool padding = rtp_check_padding(rtp_header);
    bool extension = rtp_check_extension(rtp_header);
    uint8_t cc = rtp_get_cc(rtp_header);
    bool marker = rtp_check_marker(rtp_header);
    uint8_t type = rtp_get_type(rtp_header);
    uint16_t seqnum = rtp_get_seqnum(rtp_header);
    uint32_t timestamp = rtp_get_timestamp(rtp_header);
    ptrdiff_t extension_offset = rtp_extension((uint8_t *)rtp_header) -
                                 rtp_header;
    uref_block_peek_unmap(uref, 0, rtp_buffer, rtp_header);

    if (unlikely(!valid)) {
        upipe_warn(upipe, "invalid RTP header");
        uref_free(uref);
        return;
    }

    if (padding) {
        uint8_t padding_size;
        if (unlikely(!ubase_check(uref_block_extract(uref, -1, 1,
                                                     &padding_size)))) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }
        uref_block_resize(uref, 0, -padding_size);
    }

    size_t offset = RTP_HEADER_SIZE + 4 * cc;

    if (extension) {
        rtp_header = uref_block_peek(uref, extension_offset,
                                     RTP_EXTENSION_SIZE, rtp_buffer);
        if (unlikely(rtp_header == NULL)) {
            upipe_warn(upipe, "invalid buffer received");
            uref_free(uref);
            return;
        }
        offset += 4 * (1 + rtpx_get_length(rtp_header));
        uref_block_peek_unmap(uref, extension_offset, rtp_buffer, rtp_header);
    }
    uref_block_resize(uref, offset, -1);

    if (unlikely(upipe_rtpd->expected_seqnum != -1 &&
                 seqnum != upipe_rtpd->expected_seqnum)) {
        upipe_dbg_va(upipe, "potentially lost %d RTP packets, got %u expected %u",
                     (seqnum + UINT16_MAX + 1 - upipe_rtpd->expected_seqnum) &
                     UINT16_MAX, seqnum, upipe_rtpd->expected_seqnum);
        upipe_rtpd->lost +=
            (seqnum + UINT16_MAX + 1 - upipe_rtpd->expected_seqnum) & UINT16_MAX;
        uref_flow_set_discontinuity(uref);
    }
    upipe_rtpd->expected_seqnum = seqnum + 1;
    upipe_rtpd->expected_seqnum &= UINT16_MAX;

    if (unlikely(type != upipe_rtpd->type)) {
        upipe_rtpd->type = type;
        upipe_rtpd_build_flow_def(upipe);
    }

    /* timestamp */
    switch (upipe_rtpd->mode) {
        case UPIPE_RTPD_MPA:
        case UPIPE_RTPD_MPV:
        case UPIPE_RTPD_MPEG4_AUDIO:
            if (timestamp == upipe_rtpd->last_timestamp)
                break;
            /* We set DTS because we are in coded domain and we are sure
             * DTS = PTS */
            uref_clock_set_dts_orig(uref,
                    (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate);
            uref_clock_set_dts_pts_delay(uref, 0);
            upipe_throw_clock_ts(upipe, uref);
            break;
        case UPIPE_RTPD_H264:
            if (timestamp == upipe_rtpd->last_timestamp)
                break;
            uref_clock_set_pts_orig(uref,
                    (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate);
            upipe_throw_clock_ts(upipe, uref);
            break;
        case UPIPE_RTPD_MP2T:
            upipe_throw_clock_ref(upipe, uref,
                    (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate,
                    false);
            break;
        case UPIPE_RTPD_UNKNOWN:
        case UPIPE_RTPD_PCM:
        case UPIPE_RTPD_PCMA:
        case UPIPE_RTPD_PCMU:
            if (upipe_rtpd->rate) {
                uref_clock_set_pts_orig(uref,
                        (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate);
                upipe_throw_clock_ts(upipe, uref);
            }
            break;
        case UPIPE_RTPD_GSM:
        case UPIPE_RTPD_QCELP:
            if (upipe_rtpd->rate &&
                timestamp != upipe_rtpd->last_timestamp) {
                /* We set DTS because we are in coded domain and we are sure
                 * DTS = PTS */
                uref_clock_set_dts_orig(uref,
                        (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate);
                uref_clock_set_dts_pts_delay(uref, 0);
                upipe_throw_clock_ts(upipe, uref);
            }
            break;
        case UPIPE_RTPD_OPUS:
            if (timestamp == upipe_rtpd->last_timestamp)
                break;
            /* We set DTS because we are in coded domain and we are sure
             * DTS = PTS */
            uref_clock_set_dts_orig(uref,
                    (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate);
            uref_clock_set_dts_pts_delay(uref, 0);
            upipe_throw_clock_ts(upipe, uref);
            break;
        default:
            break;
    }
    upipe_rtpd->last_timestamp = timestamp;

    /* payload */
    switch (upipe_rtpd->mode) {
        case UPIPE_RTPD_MPA:
            upipe_rtpd_output_mpa(upipe, uref, upump_p);
            break;
        case UPIPE_RTPD_MPV:
            upipe_rtpd_output_mpv(upipe, uref, upump_p);
            break;
        case UPIPE_RTPD_H264:
            upipe_rtpd_output_h264(upipe, uref, upump_p);
            break;
        case UPIPE_RTPD_MPEG4_AUDIO:
            upipe_rtpd_output_mpeg4_audio(upipe, uref, upump_p, marker);
            break;
        default:
            upipe_rtpd_output(upipe, uref, upump_p);
            break;
    }
}

/** @internal @This receives the result of a ubuf manager request.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_rtpd_check_ubuf_mgr(struct upipe *upipe,
                                     struct uref *flow_format)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;
    uref_free(upipe_rtpd->flow_def_input);
    upipe_rtpd->flow_def_input = flow_format;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_rtpd_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;

    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    int i;
    for (i = 0; upipe_rtpd_flow_defs[i] != NULL; i++)
        if (!ubase_ncmp(def, upipe_rtpd_flow_defs[i]))
            break;
    if (i > UPIPE_RTPD_UNKNOWN) {
        upipe_warn(upipe, "block. input is deprecated, please set it to block.rtp.");
        i = UPIPE_RTPD_UNKNOWN;
        uref_flow_set_def(flow_def_dup, "block.rtp.");
    }
    upipe_rtpd->mode_config = i;
    upipe_rtpd->type = UINT8_MAX;

    upipe_rtpd->rate = 0;
    uref_sound_flow_get_rate(flow_def, &upipe_rtpd->rate);
    if (upipe_rtpd->mode_config == UPIPE_RTPD_PCM && upipe_rtpd->rate == 0)
        upipe_warn(upipe, "you have to specify sample rate for PCM modes");

    uref_free(upipe_rtpd->flow_def_input);
    upipe_rtpd->flow_def_input = NULL;
    upipe_rtpd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a rtpd pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_rtpd_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);

    UBASE_HANDLED_RETURN(upipe_rtpd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtpd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_RTPD_GET_PACKETS_LOST: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_RTPD_SIGNATURE)
            uint64_t *lost = va_arg(args, uint64_t *);
            *lost = upipe_rtpd->lost;
            upipe_rtpd->lost = 0; /* reset counter */
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_rtpd_free(struct upipe *upipe)
{
    struct upipe_rtpd *upipe_rtpd = upipe_rtpd_from_upipe(upipe);
    if (upipe_rtpd->next_uref != NULL)
        upipe_rtpd_output(upipe, upipe_rtpd->next_uref, NULL);
    upipe_throw_dead(upipe);

    uref_free(upipe_rtpd->flow_def_input);
    upipe_rtpd_clean_ubuf_mgr(upipe);
    upipe_rtpd_clean_output(upipe);
    upipe_rtpd_clean_urefcount(upipe);
    upipe_rtpd_free_void(upipe);
}

static struct upipe_mgr upipe_rtpd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_RTPD_SIGNATURE,

    .upipe_alloc = upipe_rtpd_alloc,
    .upipe_input = upipe_rtpd_input,
    .upipe_control = upipe_rtpd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for rtpd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtpd_mgr_alloc(void)
{
    return &upipe_rtpd_mgr;
}

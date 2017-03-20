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
#include <upipe-modules/upipe_rtp_decaps.h>

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
    UPIPE_RTPD_MPTS,
    /** Opus */
    UPIPE_RTPD_OPUS,
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

UPIPE_HELPER_UPIPE(upipe_rtpd, upipe, UPIPE_RTPD_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_rtpd, urefcount, upipe_rtpd_free);
UPIPE_HELPER_VOID(upipe_rtpd);
UPIPE_HELPER_OUTPUT(upipe_rtpd, output, flow_def, output_state, request_list);

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
    upipe_rtpd->expected_seqnum = -1;
    upipe_rtpd->type = UINT8_MAX;
    upipe_rtpd->mode = upipe_rtpd->mode_config = UPIPE_RTPD_UNKNOWN;
    upipe_rtpd->lost = 0;
    upipe_rtpd->flow_def_input = NULL;
    upipe_rtpd->rate = 0;

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
    uint64_t rate = 0;
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
        case RTP_TYPE_TS:
            upipe_rtpd->mode = UPIPE_RTPD_MPTS;
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
    upipe_rtpd->rate = rate;

    switch (upipe_rtpd->mode) {
        case UPIPE_RTPD_MPA:
        case UPIPE_RTPD_MPV:
        case UPIPE_RTPD_MPTS:
            uref_clock_set_wrap(flow_def, POW2_32 * UCLOCK_FREQ / 90000);
            break;
        case UPIPE_RTPD_PCM:
            if (channels)
                uref_sound_flow_set_channels(flow_def, channels);
            /* intended fall-through */
        case UPIPE_RTPD_UNKNOWN:
        case UPIPE_RTPD_PCMA:
        case UPIPE_RTPD_PCMU:
        case UPIPE_RTPD_GSM:
        case UPIPE_RTPD_QCELP:
            if (upipe_rtpd->rate)
                uref_clock_set_wrap(flow_def,
                                    POW2_32 * UCLOCK_FREQ / upipe_rtpd->rate);
            break;
        case UPIPE_RTPD_OPUS:
            uref_clock_set_wrap(flow_def, POW2_32 * UCLOCK_FREQ / 48000);
            break;
    }

    upipe_rtpd_store_flow_def(upipe, flow_def);
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
    bool extension = rtp_check_extension(rtp_header);
    uint8_t cc = rtp_get_cc(rtp_header);
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

    if (unlikely(upipe_rtpd->expected_seqnum != -1 &&
                 seqnum != upipe_rtpd->expected_seqnum)) {
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

    switch (upipe_rtpd->mode) {
        case UPIPE_RTPD_MPA:
            offset += RTP2250A_HEADER_SIZE;
            /* We set DTS because we are in coded domain and we are sure
             * DTS = PTS */
            uref_clock_set_dts_orig(uref,
                                    (uint64_t)timestamp * UCLOCK_FREQ / 90000);
            uref_clock_set_dts_pts_delay(uref, 0);
            upipe_throw_clock_ts(upipe, uref);
            break;
        case UPIPE_RTPD_MPV:
            rtp_header = uref_block_peek(uref, offset,
                                         RTP2250V_HEADER_SIZE,
                                         rtp_buffer);
            if (unlikely(rtp_header == NULL)) {
                upipe_warn(upipe, "invalid buffer received");
                uref_free(uref);
                return;
            }
            offset += RTP2250V_HEADER_SIZE +
                rtp2250v_check_mpeg2(rtp_header) * RTP2250VX_HEADER_SIZE;
            uref_block_peek_unmap(uref, offset, rtp_buffer, rtp_header);
            uref_clock_set_pts_orig(uref,
                                    (uint64_t)timestamp * UCLOCK_FREQ / 90000);
            upipe_throw_clock_ts(upipe, uref);
            break;
        case UPIPE_RTPD_MPTS:
            upipe_throw_clock_ref(upipe, uref,
                                  (uint64_t)timestamp * UCLOCK_FREQ / 90000,
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
            /* We set DTS because we are in coded domain and we are sure
             * DTS = PTS */
            if (upipe_rtpd->rate) {
                uref_clock_set_dts_orig(uref,
                        (uint64_t)timestamp * UCLOCK_FREQ / upipe_rtpd->rate);
                uref_clock_set_dts_pts_delay(uref, 0);
                upipe_throw_clock_ts(upipe, uref);
            }
            break;
        case UPIPE_RTPD_OPUS:
            /* We set DTS because we are in coded domain and we are sure
             * DTS = PTS */
            uref_clock_set_dts_orig(uref,
                                    (uint64_t)timestamp * UCLOCK_FREQ / 48000);
            uref_clock_set_dts_pts_delay(uref, 0);
            upipe_throw_clock_ts(upipe, uref);
            break;
        default:
            break;
    }

    uref_block_resize(uref, offset, -1);
    upipe_rtpd_output(upipe, uref, upump_p);
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
    upipe_rtpd->flow_def_input = flow_def_dup;
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
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_rtpd_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_rtpd_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_rtpd_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_rtpd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_rtpd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_rtpd_set_output(upipe, output);
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
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    uref_free(upipe_rtpd->flow_def_input);
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

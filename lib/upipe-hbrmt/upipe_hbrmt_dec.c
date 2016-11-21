/*
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
#include <upipe/uref.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uclock.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>

#include <bitstream/ietf/rtp.h>
#include <bitstream/smpte/2022_6_hbrmt.h>

#include <upipe-hbrmt/upipe_hbrmt_dec.h>
#include "upipe_hbrmt_common.h"

/** @hidden */
static int upipe_hbrmt_dec_check(struct upipe *upipe, struct uref *flow_format);

/** upipe_hbrmt_dec structure */
struct upipe_hbrmt_dec {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** got a discontinuity */
    bool discontinuity;

    /** expected sequence number */
    int expected_seqnum;

    /** indicates next packet is a start of a frame */
    bool next_packet_frame_start;

    /** Packed block destination */
    uint8_t *dst_buf;
    int dst_size;

    /** current frame **/
    struct ubuf *ubuf;

    /** output pipe */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** frame number */
    uint64_t frame;

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_hbrmt_dec, upipe, UPIPE_HBRMT_DEC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_hbrmt_dec, urefcount, upipe_hbrmt_dec_free);
UPIPE_HELPER_VOID(upipe_hbrmt_dec);
UPIPE_HELPER_OUTPUT(upipe_hbrmt_dec, output, flow_def, output_state, request_list);

UPIPE_HELPER_UREF_MGR(upipe_hbrmt_dec, uref_mgr, uref_mgr_request,
                      upipe_hbrmt_dec_check,
                      upipe_hbrmt_dec_register_output_request,
                      upipe_hbrmt_dec_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_hbrmt_dec, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_hbrmt_dec_check,
                      upipe_hbrmt_dec_register_output_request,
                      upipe_hbrmt_dec_unregister_output_request)

/** @internal @This allocates a hbrmt_dec pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_hbrmt_dec_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_hbrmt_dec_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);
    upipe_hbrmt_dec_init_urefcount(upipe);
    upipe_hbrmt_dec_init_uref_mgr(upipe);
    upipe_hbrmt_dec_init_ubuf_mgr(upipe);
    upipe_hbrmt_dec_init_output(upipe);

    upipe_hbrmt_dec->ubuf     = NULL;
    upipe_hbrmt_dec->dst_buf  = NULL;
    upipe_hbrmt_dec->dst_size = 0;

    upipe_hbrmt_dec->next_packet_frame_start = false;
    upipe_hbrmt_dec->expected_seqnum = -1;
    upipe_hbrmt_dec->discontinuity = false;
    upipe_hbrmt_dec->frame = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal */
static int upipe_hbrmt_dec_set_fps(struct upipe *upipe, uint8_t frate)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);

    if (frate < 0x10 || frate > 0x1b) {
        upipe_err_va(upipe, "Invalid hbrmt frate 0x%x", frate);
        return UBASE_ERR_INVALID;
    }

    static const struct urational frate_fps[12] = {
        { 60,    1    }, // 0x10
        { 60000, 1001 }, // 0x11
        { 50,    1    }, // 0x12
        { 0,     0    }, // 0x13
        { 48,    1    }, // 0x14
        { 48000, 1001 }, // 0x15
        { 30,    1    }, // 0x16
        { 30000, 1001 }, // 0x17
        { 25,    1    }, // 0x18
        { 0,     0    }, // 0x19
        { 24,    1    }, // 0x1A
        { 24000, 1001 }, // 0x1B
    };

    const struct urational *fps = &frate_fps[frate - 0x10];
    if (fps->num == 0) {
        upipe_err_va(upipe, "Invalid hbrmt frate 0x%x", frate);
        return UBASE_ERR_INVALID;
    }

    struct uref *flow_format = uref_dup(upipe_hbrmt_dec->flow_def);
    uref_pic_flow_set_fps(flow_format, *fps);
    upipe_hbrmt_dec->f = sdi_get_offsets(flow_format);

    uint64_t latency;
    if (!ubase_check(uref_clock_get_latency(flow_format, &latency)))
        latency = 0;
    latency += UCLOCK_FREQ * fps->den / fps->num;
    uref_clock_set_latency(flow_format, latency);
    upipe_hbrmt_dec_store_flow_def(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal */
static int upipe_hbrmt_alloc_output_ubuf(struct upipe *upipe)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);

    const struct sdi_offsets_fmt *f = upipe_hbrmt_dec->f;

    /* Only 422 accepted, so this assumption is fine */
    uint64_t pixels = f->width * f->height * 2;
    uint64_t packed_size = (pixels * 10) / 8;

    upipe_hbrmt_dec->ubuf = ubuf_block_alloc(upipe_hbrmt_dec->ubuf_mgr, packed_size);
    if (unlikely(upipe_hbrmt_dec->ubuf == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_hbrmt_dec->dst_size = -1;
    if (unlikely(!ubase_check(ubuf_block_write(upipe_hbrmt_dec->ubuf, 0,
                        &upipe_hbrmt_dec->dst_size,
                        &upipe_hbrmt_dec->dst_buf)))) {
        ubuf_free(upipe_hbrmt_dec->ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_hbrmt_dec_input(struct upipe *upipe, struct uref *uref,
                                    struct upump **upump_p)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);

    int src_size = -1;
    const uint8_t *src = NULL;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &src_size, &src)))) {
        upipe_err(upipe, "invalid buffer received");
        goto end;
    }

    if (unlikely(src_size < HBRMT_DATA_OFFSET + HBRMT_DATA_SIZE)) {
        upipe_err(upipe, "too small buffer received");
        goto end;
    }

    bool marker = rtp_check_marker(src);
    uint16_t seqnum = rtp_get_seqnum(src);

    src_size -= RTP_HEADER_SIZE;
    const uint8_t *hbrmt = &src[RTP_HEADER_SIZE];

    if (unlikely(!upipe_hbrmt_dec->f)) {
        const uint8_t frate = smpte_hbrmt_get_frate(hbrmt);
        if (!ubase_check(upipe_hbrmt_dec_set_fps(upipe, frate)))
            goto end;
    }

    if (unlikely(upipe_hbrmt_dec->expected_seqnum != -1 &&
                 seqnum != upipe_hbrmt_dec->expected_seqnum)) {
        upipe_warn_va(upipe, "potentially lost %d RTP packets, got %u expected %u",
                      (seqnum + UINT16_MAX + 1 - upipe_hbrmt_dec->expected_seqnum) &
                      UINT16_MAX, seqnum, upipe_hbrmt_dec->expected_seqnum);
        upipe_hbrmt_dec->discontinuity = true;
        /* drop current packet */
        if (upipe_hbrmt_dec->ubuf) {
            ubuf_block_unmap(upipe_hbrmt_dec->ubuf, 0);
            ubuf_free(upipe_hbrmt_dec->ubuf);
            upipe_hbrmt_dec->ubuf = NULL;
        }
    }

    upipe_hbrmt_dec->expected_seqnum = (seqnum + 1) & UINT16_MAX;

    if (upipe_hbrmt_dec->discontinuity) {
        /* reset discontinuity */
        if (marker) {
            upipe_hbrmt_dec->discontinuity = false;
            upipe_hbrmt_dec->next_packet_frame_start = true;
        }
        goto end;
    }

    /* Allocate block memory */
    if (upipe_hbrmt_dec->next_packet_frame_start)
        if (!ubase_check(upipe_hbrmt_alloc_output_ubuf(upipe)))
            goto end;

    upipe_hbrmt_dec->next_packet_frame_start = marker;

    if (unlikely(!upipe_hbrmt_dec->ubuf))
        goto end;

    const uint8_t *payload = &hbrmt[HBRMT_HEADER_ONLY_SIZE];
    if (smpte_hbrmt_get_clock_frequency(hbrmt)) {
        payload += 4;
        src_size -= 4;
    }
    uint8_t ext = smpte_hbrmt_get_ext(hbrmt);
    if (ext) {
        payload += 4 * ext;
        src_size -= 4 * ext;
    }

    int to_write = HBRMT_DATA_SIZE;

    if (to_write > src_size) {
        to_write = src_size;
        upipe_err(upipe, "Too small packet, reading anyway");
    }

    if (to_write > upipe_hbrmt_dec->dst_size) {
        to_write = upipe_hbrmt_dec->dst_size;
        if (!marker)
            upipe_err(upipe, "Not overflowing output packet");
    }

    memcpy(upipe_hbrmt_dec->dst_buf, payload, to_write);
    upipe_hbrmt_dec->dst_buf += to_write;
    upipe_hbrmt_dec->dst_size -= to_write;

    if (!marker)
        goto end;

    /* Output a block */
    uref_block_unmap(uref, 0);

    const struct urational *fps = &upipe_hbrmt_dec->f->fps;
    uint64_t pts = UINT32_MAX + upipe_hbrmt_dec->frame++ * fps->den / fps->num;

    uref_clock_set_pts_orig(uref, pts);
    uref_clock_set_pts_prog(uref, pts);

    // FIXME: we don't have a clock ref
//    upipe_throw_clock_ref(upipe, uref, last_rtp_timestamp, 0);
    upipe_throw_clock_ts(upipe, uref);

    uref_attach_ubuf(uref, upipe_hbrmt_dec->ubuf);

    upipe_hbrmt_dec_output(upipe, uref, upump_p);
    upipe_hbrmt_dec->ubuf = NULL;
    return;

end:
    if (src)
        uref_block_unmap(uref, 0);
    uref_free(uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_hbrmt_dec_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "block."))

    return UBASE_ERR_NONE;
}

/** @internal @This receives a provided ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_hbrmt_dec_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_hbrmt_dec_store_flow_def(upipe, flow_format);

    if (upipe_hbrmt_dec->uref_mgr == NULL) {
        upipe_hbrmt_dec_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_hbrmt_dec->ubuf_mgr)
        return UBASE_ERR_NONE;

    flow_format = uref_block_flow_alloc_def(upipe_hbrmt_dec->uref_mgr, NULL);
    if (unlikely(flow_format == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_hbrmt_dec_require_ubuf_mgr(upipe, flow_format);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a hbrmt_dec pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_hbrmt_dec_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_hbrmt_dec_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_hbrmt_dec_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_hbrmt_dec_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_hbrmt_dec_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_hbrmt_dec_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_hbrmt_dec_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static int upipe_hbrmt_dec_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_hbrmt_dec_control(upipe, command, args));

    return upipe_hbrmt_dec_check(upipe, NULL);
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_hbrmt_dec_free(struct upipe *upipe)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);

    upipe_hbrmt_dec_clean_output(upipe);
    upipe_hbrmt_dec_clean_urefcount(upipe);
    upipe_hbrmt_dec_free_void(upipe);
}

static struct upipe_mgr upipe_hbrmt_dec_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HBRMT_DEC_SIGNATURE,

    .upipe_alloc = upipe_hbrmt_dec_alloc,
    .upipe_input = upipe_hbrmt_dec_input,
    .upipe_control = upipe_hbrmt_dec_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for hbrmt_dec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_hbrmt_dec_mgr_alloc(void)
{
    return &upipe_hbrmt_dec_mgr;
}

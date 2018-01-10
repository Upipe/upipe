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

#include "sdidec.h"

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

    /** unpack */
    void (*sdi_to_uyvy)(const uint8_t *src, uint16_t *y, uintptr_t pixels);

    /** unpack scratch buffer */
    uint8_t unpack_scratch_buffer[5];

    /** bytes in scratch buffer */
    uint8_t unpack_scratch_buffer_count;
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
    upipe_hbrmt_dec->f = NULL;

    upipe_hbrmt_dec->expected_seqnum = -1;
    upipe_hbrmt_dec->discontinuity = false;
    upipe_hbrmt_dec->frame = 0;
    upipe_hbrmt_dec->unpack_scratch_buffer_count = 0;

    upipe_hbrmt_dec->sdi_to_uyvy = upipe_sdi_to_uyvy_c;

#if defined(HAVE_X86_ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3"))
        upipe_hbrmt_dec->sdi_to_uyvy = upipe_sdi_to_uyvy_unaligned_ssse3;

    if (__builtin_cpu_supports("avx2"))
        upipe_hbrmt_dec->sdi_to_uyvy = upipe_sdi_to_uyvy_unaligned_avx2;
#endif
#endif

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal */
static int upipe_hbrmt_dec_set_flow(struct upipe *upipe, uint8_t frate, uint8_t frame)
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
    if (frame == 0x10) {
        uref_pic_flow_set_hsize(flow_format, 720);
        uref_pic_flow_set_vsize(flow_format, 486);
    } else if (frame == 0x11) {
        uref_pic_flow_set_hsize(flow_format, 720);
        uref_pic_flow_set_vsize(flow_format, 576);
    } else if (frame >= 0x20 && frame <= 0x22) {
        uref_pic_flow_set_hsize(flow_format, 1920);
        uref_pic_flow_set_vsize(flow_format, 1080);
    } else if (frame >= 0x23 && frame <= 0x24) {
        uref_pic_flow_set_hsize(flow_format, 2048);
        uref_pic_flow_set_vsize(flow_format, 1080);
    } else if (frame == 0x30) {
        uref_pic_flow_set_hsize(flow_format, 1280);
        uref_pic_flow_set_vsize(flow_format, 720);
    } else {
        upipe_err_va(upipe, "Invalid hbrmt frame 0x%x", frame);
        uref_free(flow_format);
        return UBASE_ERR_INVALID;
    }

    upipe_hbrmt_dec->f = sdi_get_offsets(flow_format);
    if (!upipe_hbrmt_dec->f) {
        upipe_err(upipe, "Couldn't figure out sdi offsets");
        uref_free(flow_format);
        return UBASE_ERR_INVALID;
    }

    uint64_t latency;
    if (!ubase_check(uref_clock_get_latency(flow_format, &latency)))
        latency = 0;
    latency += UCLOCK_FREQ * fps->den / fps->num;
    uref_clock_set_latency(flow_format, latency);
    upipe_hbrmt_dec_store_flow_def(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal */
static int upipe_hbrmt_dec_alloc_output_ubuf(struct upipe *upipe)
{
    struct upipe_hbrmt_dec *upipe_hbrmt_dec = upipe_hbrmt_dec_from_upipe(upipe);

    const struct sdi_offsets_fmt *f = upipe_hbrmt_dec->f;

    /* Only 422 accepted, so this assumption is fine */
    uint64_t samples = f->width * f->height * 2;

    upipe_hbrmt_dec->ubuf = ubuf_block_alloc(upipe_hbrmt_dec->ubuf_mgr, 2 * samples);
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
    bool marker = false;

    int src_size = -1;
    const uint8_t *src = NULL;
    if (unlikely(!ubase_check(uref_block_read(uref, 0, &src_size, &src)))) {
        upipe_err(upipe, "invalid buffer received");
        uref_free(uref);
        return;
    }

    if (unlikely(src_size < HBRMT_DATA_OFFSET + HBRMT_DATA_SIZE)) {
        upipe_err(upipe, "too small buffer received");
        goto end;
    }

    marker = rtp_check_marker(src);
    uint16_t seqnum = rtp_get_seqnum(src);

    src_size -= RTP_HEADER_SIZE;
    const uint8_t *hbrmt = &src[RTP_HEADER_SIZE];

    if (unlikely(!upipe_hbrmt_dec->f)) {
        const uint8_t frate = smpte_hbrmt_get_frate(hbrmt);
        const uint8_t frame = smpte_hbrmt_get_frame(hbrmt);
        if (!ubase_check(upipe_hbrmt_dec_set_flow(upipe, frate, frame)))
            goto end;
    }

    if (unlikely(upipe_hbrmt_dec->expected_seqnum != -1 &&
                 seqnum != upipe_hbrmt_dec->expected_seqnum)) {
        upipe_warn_va(upipe, "potentially lost %d RTP packets, got %u expected %u",
                      (seqnum + UINT16_MAX + 1 - upipe_hbrmt_dec->expected_seqnum) &
                      UINT16_MAX, seqnum, upipe_hbrmt_dec->expected_seqnum);
        upipe_hbrmt_dec->discontinuity = true;
    }

    upipe_hbrmt_dec->expected_seqnum = (seqnum + 1) & UINT16_MAX;

    if (upipe_hbrmt_dec->discontinuity)
        goto end;

    if (unlikely(!upipe_hbrmt_dec->ubuf))
        goto end;

    src_size -= HBRMT_HEADER_SIZE;
    const uint8_t *payload = &hbrmt[HBRMT_HEADER_SIZE];
    if (smpte_hbrmt_get_clock_frequency(hbrmt)) {
        payload += 4;
        src_size -= 4;
    }
    uint8_t ext = smpte_hbrmt_get_ext(hbrmt);
    if (ext) {
        payload += 4 * ext;
        src_size -= 4 * ext;
    }

    if (src_size < HBRMT_DATA_SIZE) {
        upipe_err(upipe, "Too small packet, reading anyway");
    }
    int foo = src_size;

    /* If there is data in the scratch buffer... */

    unsigned n = upipe_hbrmt_dec->unpack_scratch_buffer_count;
    if (n && upipe_hbrmt_dec->dst_size > 4 * 2) {
        /* Copy from the new "packet" into the end... */
        memcpy(&upipe_hbrmt_dec->unpack_scratch_buffer[n], payload, 5 - n);

        /* Advance input buffer. */
        payload += 5-n;
        src_size -= 5-n;

        /* Unpack from the scratch buffer. */
        upipe_sdi_to_uyvy_c(upipe_hbrmt_dec->unpack_scratch_buffer,
                (uint16_t*)upipe_hbrmt_dec->dst_buf, 2);

        /* Advance output buffer by 2 pixels */
        upipe_hbrmt_dec->dst_buf += 4 * 2;
        upipe_hbrmt_dec->dst_size -= 4 * 2;
        //printf("HI SCRATCH 2\n");

        /* Set scratch count to 0. */
        upipe_hbrmt_dec->unpack_scratch_buffer_count = 0;
    }

    if (src_size > upipe_hbrmt_dec->dst_size * 5 / 8) {
        src_size = upipe_hbrmt_dec->dst_size * 5 / 8;
        if (!marker)
            upipe_err_va(upipe, "Not overflowing output packet: %d, %d",
                    src_size, upipe_hbrmt_dec->dst_size);
    }

    int unpack_bytes = (src_size / 5) * 5;
    int unpack_pixels = (unpack_bytes * 2) / 5;
    upipe_hbrmt_dec->sdi_to_uyvy(payload, (uint16_t*)upipe_hbrmt_dec->dst_buf, unpack_pixels);
    upipe_hbrmt_dec->dst_buf += 4 * unpack_pixels;
    upipe_hbrmt_dec->dst_size -= 4 * unpack_pixels;

    /* If we have any bytes remaining... */
    if (unpack_bytes < src_size) {
        /* Copy them into the scratch buffer. */
        memcpy(upipe_hbrmt_dec->unpack_scratch_buffer, &payload[unpack_bytes],
                src_size - unpack_bytes);
        upipe_hbrmt_dec->unpack_scratch_buffer_count = src_size - unpack_bytes;
    }

end:
    uref_block_unmap(uref, 0);

    bool output = marker || upipe_hbrmt_dec->discontinuity;
    if (output && upipe_hbrmt_dec->ubuf) {
        /* output current block */
        ubuf_block_unmap(upipe_hbrmt_dec->ubuf, 0);
        uref_attach_ubuf(uref, upipe_hbrmt_dec->ubuf);
        upipe_hbrmt_dec->ubuf = NULL;
        upipe_hbrmt_dec_output(upipe, uref, upump_p);
    } else
        uref_free(uref);

    if (marker) {
        /* reset discontinuity when we see the next marker */
        upipe_hbrmt_dec->discontinuity = false;
        upipe_hbrmt_dec_alloc_output_ubuf(upipe);
    }
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
    upipe_throw_dead(upipe);

    ubuf_free(upipe_hbrmt_dec->ubuf);

    upipe_hbrmt_dec_clean_uref_mgr(upipe);
    upipe_hbrmt_dec_clean_ubuf_mgr(upipe);
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

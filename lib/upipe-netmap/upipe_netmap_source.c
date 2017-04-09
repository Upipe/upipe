/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *          Rostislav Pehlivanov
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
 * @short Upipe source module for netmap sockets
 */


#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-netmap/upipe_netmap_source.h>

#include <net/if.h>

#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>

#include <poll.h>

#include <bitstream/ieee/ethernet.h>
#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>
#include <bitstream/ietf/rtp.h>

/** @hidden */
static int upipe_netmap_source_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a netmap source pipe. */
struct upipe_netmap_source {
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

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;

    /** netmap descriptor **/
    struct nm_desc *d;

    /** netmap uri **/
    char *uri;

    /** netmap ring **/
    unsigned int ring_idx;

    /** got a discontinuity */
    bool discontinuity;

    /** expected sequence number */
    int expected_seqnum;

    /** Packed block destination */
    uint8_t *dst_buf;
    int dst_size;

    /** current frame **/
    struct uref *uref;
    
    /** frame number */
    uint64_t frame;

    /* SDI offsets */
    const struct sdi_offsets_fmt *f;

    /** unpack */
    void (*sdi_to_uyvy)(const uint8_t *src, uint16_t *y, uintptr_t pixels);

    /** unpack scratch buffer */
    uint8_t unpack_scratch_buffer[5];

    /** bytes in scratch buffer */
    uint8_t unpack_scratch_buffer_count;
    
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_netmap_source, upipe, UPIPE_NETMAP_SOURCE_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_netmap_source, urefcount, upipe_netmap_source_free)
UPIPE_HELPER_VOID(upipe_netmap_source)

UPIPE_HELPER_OUTPUT(upipe_netmap_source, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_netmap_source, uref_mgr, uref_mgr_request,
                      upipe_netmap_source_check,
                      upipe_netmap_source_register_output_request,
                      upipe_netmap_source_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_netmap_source, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_netmap_source_check,
                      upipe_netmap_source_register_output_request,
                      upipe_netmap_source_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_netmap_source, uclock, uclock_request, upipe_netmap_source_check,
                    upipe_netmap_source_register_output_request,
                    upipe_netmap_source_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_netmap_source, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_netmap_source, upump, upump_mgr)

/** @internal @This allocates a netmap source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_netmap_source_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_netmap_source_alloc_void(mgr, uprobe, signature, args);
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    upipe_netmap_source_init_urefcount(upipe);
    upipe_netmap_source_init_uref_mgr(upipe);
    upipe_netmap_source_init_ubuf_mgr(upipe);
    upipe_netmap_source_init_output(upipe);
    upipe_netmap_source_init_upump_mgr(upipe);
    upipe_netmap_source_init_upump(upipe);
    upipe_netmap_source_init_uclock(upipe);
    upipe_netmap_source->uri = NULL;
    upipe_netmap_source->d = NULL;
    
    upipe_netmap_source->uref     = NULL;
    upipe_netmap_source->dst_buf  = NULL;
    upipe_netmap_source->dst_size = 0;
    upipe_netmap_source->f = NULL;

    upipe_netmap_source->expected_seqnum = -1;
    upipe_netmap_source->discontinuity = false;
    upipe_netmap_source->frame = 0;
    upipe_netmap_source->unpack_scratch_buffer_count = 0;

    upipe_netmap_source->sdi_to_uyvy = upipe_sdi_unpack_c;

#if defined(__i686__) || defined(__x86_64__)
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
            upipe_netmap_source->sdi_to_uyvy = upipe_sdi_to_uyvy_unaligned_ssse3;

    if (__builtin_cpu_supports("avx2"))
        upipe_netmap_source->sdi_to_uyvy = upipe_sdi_to_uyvy_unaligned_avx2;
#endif
#endif  
    
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal */
static int upipe_netmap_source_set_flow(struct upipe *upipe, uint8_t frate, uint8_t frame)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

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

    struct uref *flow_format = uref_dup(upipe_netmap_source->flow_def);
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

    upipe_netmap_source->f = sdi_get_offsets(flow_format);
    if (!upipe_netmap_source->f) {
        upipe_err(upipe, "Couldn't figure out sdi offsets");
        uref_free(flow_format);
        return UBASE_ERR_INVALID;
    }

    uint64_t latency;
    if (!ubase_check(uref_clock_get_latency(flow_format, &latency)))
        latency = 0;
    latency += UCLOCK_FREQ * fps->den / fps->num;
    uref_clock_set_latency(flow_format, latency);
    upipe_netmap_source_store_flow_def(upipe, flow_format);

    return UBASE_ERR_NONE;
}

/** @internal */
static int upipe_netmap_source_alloc_output_uref(struct upipe *upipe)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    const struct sdi_offsets_fmt *f = upipe_netmap_source->f;

    /* Only 422 accepted, so this assumption is fine */
    uint64_t samples = f->width * f->height * 2;

    upipe_netmap_source->uref = uref_block_alloc(upipe_netmap_source->uref_mgr,
                                                 upipe_netmap_source->ubuf_mgr,
                                                 2 * samples);
    if (unlikely(upipe_netmap_source->uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_netmap_source->dst_size = -1;
    if (unlikely(!ubase_check(uref_block_write(upipe_netmap_source->uref, 0,
                                               &upipe_netmap_source->dst_size,
                                               &upipe_netmap_source->dst_buf)))) {
        uref_free(upipe_netmap_source->uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    return UBASE_ERR_NONE;
}

static inline void handle_hbrmt_packet(struct upipe *upipe, struct upump *upump, 
                                       uint8_t *src, uint16_t src_size)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    bool marker = false;

    marker = rtp_check_marker(src);
    uint16_t seqnum = rtp_get_seqnum(src);

    src_size -= RTP_HEADER_SIZE;
    const uint8_t *hbrmt = &src[RTP_HEADER_SIZE];

    if (unlikely(!upipe_netmap_source->f)) {
        const uint8_t frate = smpte_hbrmt_get_frate(hbrmt);
        const uint8_t frame = smpte_hbrmt_get_frame(hbrmt);
        if (!ubase_check(upipe_netmap_source_set_flow(upipe, frate, frame)))
            goto end;
    }

    if (unlikely(upipe_netmap_source->expected_seqnum != -1 &&
                 seqnum != upipe_netmap_source->expected_seqnum)) {
        upipe_warn_va(upipe, "potentially lost %d RTP packets, got %u expected %u",
                      (seqnum + UINT16_MAX + 1 - upipe_netmap_source->expected_seqnum) &
                      UINT16_MAX, seqnum, upipe_netmap_source->expected_seqnum);
        upipe_netmap_source->discontinuity = true;
    }

    upipe_netmap_source->expected_seqnum = (seqnum + 1) & UINT16_MAX;

    if (upipe_netmap_source->discontinuity)
        goto end;

    if (unlikely(!upipe_netmap_source->uref))
        goto end;

    src_size -= HBRMT_HEADER_ONLY_SIZE;
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

    if (src_size < HBRMT_DATA_SIZE) {
        upipe_err(upipe, "Too small packet, reading anyway");
    }
    int foo = src_size;

    /* If there is data in the scratch buffer... */

    unsigned n = upipe_netmap_source->unpack_scratch_buffer_count;
    if (n && upipe_netmap_source->dst_size > 4 * 2) {
        /* Copy from the new "packet" into the end... */
        memcpy(&upipe_netmap_source->unpack_scratch_buffer[n], payload, 5 - n);

        /* Advance input buffer. */
        payload += 5-n;
        src_size -= 5-n;

        /* Unpack from the scratch buffer. */
        upipe_sdi_unpack_c(upipe_netmap_source->unpack_scratch_buffer,
                (uint16_t*)upipe_netmap_source->dst_buf, 2);

        /* Advance output buffer by 2 pixels */
        upipe_netmap_source->dst_buf += 4 * 2;
        upipe_netmap_source->dst_size -= 4 * 2;
        //printf("HI SCRATCH 2\n");

        /* Set scratch count to 0. */
        upipe_netmap_source->unpack_scratch_buffer_count = 0;
    }

    if (src_size > upipe_netmap_source->dst_size * 5 / 8) {
        src_size = upipe_netmap_source->dst_size * 5 / 8;
        if (!marker)
            upipe_err_va(upipe, "Not overflowing output packet: %d, %d",
                    src_size, upipe_netmap_source->dst_size);
    }

    int unpack_bytes = (src_size / 5) * 5;
    int unpack_pixels = (unpack_bytes * 2) / 5;
    upipe_netmap_source->sdi_to_uyvy(payload, (uint16_t*)upipe_netmap_source->dst_buf, unpack_pixels);
    upipe_netmap_source->dst_buf += 4 * unpack_pixels;
    upipe_netmap_source->dst_size -= 4 * unpack_pixels;

    /* If we have any bytes remaining... */
    if (unpack_bytes < src_size) {
        /* Copy them into the scratch buffer. */
        memcpy(upipe_netmap_source->unpack_scratch_buffer, &payload[unpack_bytes],
                src_size - unpack_bytes);
        upipe_netmap_source->unpack_scratch_buffer_count = src_size - unpack_bytes;
    }

end:
    if ((marker || upipe_netmap_source->discontinuity) && upipe_netmap_source->uref) {
        /* output current block */
        uref_block_unmap(upipe_netmap_source->uref, 0);
        upipe_netmap_source_output(upipe, upipe_netmap_source->uref, &upipe_netmap_source->upump);
        upipe_netmap_source->uref = NULL;
    }

    if (marker) {
        /* reset discontinuity when we see the next marker */
        upipe_netmap_source->discontinuity = false;
        upipe_netmap_source_alloc_output_uref(upipe);
    }
}

static void upipe_netmap_source_worker(struct upump *upump, uint8_t *pkt)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    uint64_t systime = 0;
    if (likely(upipe_netmap_source->uclock != NULL))
        systime = uclock_now(upipe_netmap_source->uclock);

    ioctl(NETMAP_FD(upipe_netmap_source->d), NIOCRXSYNC, NULL);
    struct netmap_ring *rxring = NETMAP_RXRING(upipe_netmap_source->d->nifp,
            upipe_netmap_source->ring_idx);

    uint64_t pkts = 0;
    while (!nm_ring_empty(rxring)) {
        const uint32_t cur = rxring->cur;
        uint8_t *src = (uint8_t*)NETMAP_BUF(rxring, rxring->slot[cur].buf_idx);

        if (ethernet_get_lentype(src) != ETHERNET_TYPE_IP)
            goto next;

        uint8_t *ip = &src[ETHERNET_HEADER_LEN];
        if (ip_get_proto(ip) != IP_PROTO_UDP)
            goto next;

        uint8_t *udp = ip_payload(ip);
        const uint8_t *rtp = udp_payload(udp);
        uint16_t payload_len = udp_get_len(udp) - UDP_HEADER_SIZE;
        
        // XXX: verify packet length

        

next:
        rxring->head = rxring->cur = nm_ring_next(rxring, cur);
    }   
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_netmap_source_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_netmap_source_store_flow_def(upipe, flow_format);

    upipe_netmap_source_check_upump_mgr(upipe);
    if (upipe_netmap_source->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_netmap_source->uref_mgr == NULL) {
        upipe_netmap_source_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_netmap_source->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_netmap_source->uref_mgr, NULL);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_netmap_source_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_netmap_source->uclock == NULL &&
        urequest_get_opaque(&upipe_netmap_source->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (!upipe_netmap_source->d)
        return UBASE_ERR_NONE;

    if (NETMAP_FD(upipe_netmap_source->d) == -1)
        return UBASE_ERR_NONE;

    if (upipe_netmap_source->upump)
        return UBASE_ERR_NONE;

    struct upump *upump = upump_alloc_timer(upipe_netmap_source->upump_mgr,
            upipe_netmap_source_worker, upipe, upipe->refcount, 0,
            UCLOCK_FREQ/1000);

    upipe_netmap_source_set_upump(upipe, upump);
    upump_start(upump);

    return UBASE_ERR_NONE;
}

/** @internal @This returns the uri of the currently opened netmap.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the netmap source
 * @return an error code
 */
static int upipe_netmap_source_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_netmap_source->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given netmap source.
 *
 * @param upipe description structure of the pipe
 * @param uri of the netmap source
 * @return an error code
 */
static int upipe_netmap_source_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    nm_close(upipe_netmap_source->d);
    ubase_clean_str(&upipe_netmap_source->uri);
    upipe_netmap_source_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    if (sscanf(uri, "%*[^-]-%u/R", &upipe_netmap_source->ring_idx) != 1) {
        upipe_err_va(upipe, "invalid netmap receive uri %s", uri);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_netmap_source->d = nm_open(uri, NULL, 0, 0);
    if (unlikely(!upipe_netmap_source->d)) {
        upipe_err_va(upipe, "can't open netmap socket %s", uri);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_netmap_source->uri = strdup(uri);
    if (unlikely(upipe_netmap_source->uri == NULL)) {
        nm_close(upipe_netmap_source->d);
        upipe_netmap_source->d = NULL;
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_notice_va(upipe, "opening netmap socket %s ring %u",
            upipe_netmap_source->uri, upipe_netmap_source->ring_idx);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a netmap source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_netmap_source_control(struct upipe *upipe,
                                 int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_netmap_source_set_upump(upipe, NULL);
            return upipe_netmap_source_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_netmap_source_set_upump(upipe, NULL);
            upipe_netmap_source_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_netmap_source_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_netmap_source_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_netmap_source_set_output(upipe, output);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_netmap_source_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_netmap_source_set_uri(upipe, uri);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a netmap source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_netmap_source_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_netmap_source_control(upipe, command, args));

    return upipe_netmap_source_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_netmap_source_free(struct upipe *upipe)
{
    struct upipe_netmap_source *upipe_netmap_source = upipe_netmap_source_from_upipe(upipe);

    nm_close(upipe_netmap_source->d);

    upipe_throw_dead(upipe);

    // FIXME handle mapped uref
    uref_free(upipe_netmap_source->uref);
    free(upipe_netmap_source->uri);
    upipe_netmap_source_clean_uclock(upipe);
    upipe_netmap_source_clean_upump(upipe);
    upipe_netmap_source_clean_upump_mgr(upipe);
    upipe_netmap_source_clean_output(upipe);
    upipe_netmap_source_clean_ubuf_mgr(upipe);
    upipe_netmap_source_clean_uref_mgr(upipe);
    upipe_netmap_source_clean_urefcount(upipe);
    upipe_netmap_source_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_netmap_source_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NETMAP_SOURCE_SIGNATURE,

    .upipe_alloc = upipe_netmap_source_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_netmap_source_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all netmap sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_netmap_source_mgr_alloc(void)
{
    return &upipe_netmap_source_mgr;
}

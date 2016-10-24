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
 * @short Upipe sink module for netmap
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubits.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-netmap/upipe_netmap_sink.h>

#include <net/if.h>
#include <arpa/inet.h>

#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>

#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>
#include <bitstream/ietf/rfc4175.h>
#include <bitstream/smpte/2022_6_hbrmt.h>
#include <bitstream/ietf/rtp.h>
#include <bitstream/ieee/ethernet.h>

#include "sdi.h"

#define HBRMT_HEADER_ONLY_SIZE 8
#define HBRMT_DATA_SIZE 1376
#define HBRMT_LEN (ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE + RTP_HEADER_SIZE + HBRMT_HEADER_ONLY_SIZE + HBRMT_DATA_SIZE)

#define UPIPE_RFC4175_MAX_PLANES 3

#define UPIPE_RFC4175_PIXEL_PAIR_BYTES 5
#define UPIPE_RFC4175_BLOCK_SIZE 15

static void upipe_planar_to_sdi_8_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
{
    for (int j = 0; j < width/2; j++) {
        uint8_t u1 = *u++;
        uint8_t v1 = *v++;
        uint8_t y1 = *y++;
        uint8_t y2 = *y++;

        *l++ = u1;                                  // uuuuuuuu
        *l++ = y1 >> 2;                             // 00yyyyyy
        *l++ = (y1 & 0x3) << 6 | ((v1 >> 4) & 0xf); // yy00vvvv
        *l++ = (v1 << 4) | (y2 >> 6);               // vvvv00yy
        *l++ = y2 << 2;                             // yyyyyy00
    }
}

static void upipe_planar_to_sdi_10_c(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width)
{
    for (int j = 0; j < width/2; j++) {
        uint16_t u1 = *u++;
        uint16_t v1 = *v++;
        uint16_t y1 = *y++;
        uint16_t y2 = *y++;

        *l++ = (u1 >> 2) & 0xff;                        // uuuuuuuu
        *l++ = ((u1 & 0x3) << 6) | ((y1 >> 4) & 0x3f);  // uuyyyyyy
        *l++ = ((y1 & 0xf) << 4) | ((v1 >> 6) & 0xf);   // yyyyvvvv
        *l++ = ((v1 & 0xf) << 4) | ((y2 >> 8) & 0x3);   // vvvvvvyy
        *l++ = y2 & 0xff;                               // yyyyyyyy
    }
}

static void upipe_v210_sdi_unpack_c(const uint32_t *src, uint8_t *sdi, int64_t width)
{
#define WRITE_SDI \
    do { \
        uint32_t val = *src++; \
        uint16_t a =  val & 0x3FF; \
        uint16_t b = (val >> 10) & 0x3FF; \
        uint16_t c = (val >> 20) & 0x3FF; \
        ubits_put(&s, 30, (a << 20) | (b << 10) | c); \
    } while (0)

    struct ubits s;
    ubits_init(&s, sdi, (width*10*2) >> 3);
    for (int i = 0; i < width-5; i += 6) {
        WRITE_SDI;
        WRITE_SDI;
        WRITE_SDI;
        WRITE_SDI;
    }
    ubits_clean(&s, &sdi);
#undef WRITE_SDI
}

/** @hidden */
static bool upipe_netmap_sink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);

/** @internal @This is the private context of a netmap sink pipe. */
struct upipe_netmap_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /* Determined by the input flow_def */
    bool rfc4175;
    int input_bit_depth;
    bool input_is_v210;

    /** sequence number **/
    uint64_t seqnum;
    int64_t frame_count;

    /** Source */
    uint16_t src_port;
    in_addr_t src_ip;
    uint8_t src_mac[6];

    /** Destination */
    uint16_t dst_port;
    in_addr_t dst_ip;
    uint8_t dst_mac[6];

    /** Ring */
    unsigned int ring_idx;

    /** tr-03 stuff */
    int line; /* zero-indexed for consistency with below */
    int pixel_offset;

    /** input chroma map */
    const char *input_chroma_map[UPIPE_RFC4175_MAX_PLANES];
    const uint8_t *pixel_buffers[UPIPE_RFC4175_MAX_PLANES];
    size_t         strides[UPIPE_RFC4175_MAX_PLANES];

    /* Gets set during init only */
    int output_pixels_per_block;
    int output_block_size;

    /** tr-03 functions */
    void (*pack_8_planar)(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width);
    void (*pack_10_planar)(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);
    void (*unpack_v210)(const uint32_t *src, uint8_t *dst, int64_t width);

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** socket uri */
    char *uri;
    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** netmap descriptor **/
    struct nm_desc *d;

    /** uref sink queue **/
    struct uchain sink_queue;
    size_t n;

    /** currently used uref */
    struct uref *uref;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_netmap_sink, upipe, UPIPE_NETMAP_SINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_netmap_sink, urefcount, upipe_netmap_sink_free)
UPIPE_HELPER_VOID(upipe_netmap_sink)
UPIPE_HELPER_UPUMP_MGR(upipe_netmap_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_netmap_sink, upump, upump_mgr)
UPIPE_HELPER_INPUT(upipe_netmap_sink, urefs, nb_urefs, max_urefs, blockers, upipe_netmap_sink_output)
UPIPE_HELPER_UCLOCK(upipe_netmap_sink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)


static void upipe_udp_raw_fill_headers(struct upipe *upipe,
                                       uint8_t *header,
                                       in_addr_t ipsrc, in_addr_t ipdst,
                                       uint16_t portsrc, uint16_t portdst,
                                       uint8_t ttl, uint8_t tos, uint16_t len)
{
    ip_set_version(header, 4);
    ip_set_ihl(header, 5);
    ip_set_tos(header, tos);
    ip_set_len(header, len + UDP_HEADER_SIZE + IP_HEADER_MINSIZE);
    ip_set_id(header, 0);
    ip_set_flag_reserved(header, 0);
    ip_set_flag_mf(header, 0);
    ip_set_flag_df(header, 1);
    ip_set_frag_offset(header, 0);
    ip_set_ttl(header, ttl);
    ip_set_proto(header, IPPROTO_UDP);
    ip_set_cksum(header, 0);
    ip_set_srcaddr(header, ntohl(ipsrc));
    ip_set_dstaddr(header, ntohl(ipdst));

    header += IP_HEADER_MINSIZE;
    udp_set_srcport(header, portsrc);
    udp_set_dstport(header, portdst);
    udp_set_len(header, len + UDP_HEADER_SIZE);
    udp_set_cksum(header, 0);
}

/** @internal @This allocates a netmap sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_netmap_sink_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_netmap_sink_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    upipe_netmap_sink->seqnum = 0;
    upipe_netmap_sink->frame_count = 0;
    upipe_netmap_sink->line = 0;
    upipe_netmap_sink->pixel_offset = 0;

    upipe_netmap_sink_init_urefcount(upipe);
    upipe_netmap_sink_init_upump_mgr(upipe);
    upipe_netmap_sink_init_upump(upipe);
    upipe_netmap_sink_init_input(upipe);
    upipe_netmap_sink_init_uclock(upipe);
    upipe_netmap_sink->uri = NULL;
    upipe_netmap_sink->d = NULL;
    ulist_init(&upipe_netmap_sink->sink_queue);
    upipe_netmap_sink->n = 0;
    upipe_netmap_sink->uref = NULL;

    upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_c;
    upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_c;
    upipe_netmap_sink->unpack_v210 = upipe_v210_sdi_unpack_c;

    if (__builtin_cpu_supports("avx")) {
        upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_avx;
        upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_avx;
        upipe_netmap_sink->unpack_v210 = upipe_v210_sdi_unpack_aligned_avx;
    }

    upipe_throw_ready(upipe);
    return upipe;
}

static int upipe_netmap_put_headers(struct upipe_netmap_sink *upipe_netmap_sink,
                                    uint8_t *buf, uint16_t payload_size, uint8_t pt, bool put_marker)
{
    /* Destination MAC */
    memcpy(&buf[0], upipe_netmap_sink->dst_mac, 6);

    /* Source MAC */
    memcpy(&buf[6], upipe_netmap_sink->src_mac, 6);

    /* Ethertype */
    buf[12] = 0x08;
    buf[13] = 0x00;

    buf += ETHERNET_HEADER_LEN;

    /* 0x1c - Standard, low delay, high throughput, high reliability TOS */
    upipe_udp_raw_fill_headers(NULL, buf, upipe_netmap_sink->src_ip,
                               upipe_netmap_sink->dst_ip,
                               upipe_netmap_sink->src_port,
                               upipe_netmap_sink->dst_port,
                               10, 0x1c, payload_size);

    buf += IP_HEADER_MINSIZE + UDP_HEADER_SIZE;

    /* RTP HEADER */
    memset(buf, 0, RTP_HEADER_SIZE);
    rtp_set_hdr(buf);
    rtp_set_type(buf, pt);
    rtp_set_seqnum(buf, upipe_netmap_sink->seqnum & UINT16_MAX);
    //FIXME: NTSC only, correct this for PAL and in general
    int64_t timestamp = upipe_netmap_sink->frame_count*(int64_t)900900;
    rtp_set_timestamp(buf, timestamp & UINT32_MAX);
    if (put_marker)
        rtp_set_marker(buf);

    buf += RTP_HEADER_SIZE;

    return ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE + RTP_HEADER_SIZE;
}

static int upipe_put_hbrmt_headers(struct upipe_netmap_sink *upipe_netmap_sink,
                                   uint8_t *buf)
{
    memset(buf, 0, HBRMT_HEADER_ONLY_SIZE);
    smpte_hbrmt_set_ext(buf, 0);
    smpte_hbrmt_set_video_source_format(buf);
    smpte_hbrmt_set_video_source_id(buf, 0);
    smpte_hbrmt_set_frame_count(buf, upipe_netmap_sink->frame_count & UINT8_MAX);
    smpte_hbrmt_set_reference_for_time_stamp(buf, 0);
    smpte_hbrmt_set_video_payload_scrambling(buf, 0);
    smpte_hbrmt_set_fec(buf, 0);
    smpte_hbrmt_set_clock_frequency(buf, 0);
    smpte_hbrmt_set_map(buf, 0);
    smpte_hbrmt_set_frame(buf, 0x20); // FIXME
    smpte_hbrmt_set_frate(buf, 0x17); // FIXME
    smpte_hbrmt_set_sample(buf, 0x1);
    smpte_hbrmt_set_fmt_reserve(buf);

    buf += HBRMT_HEADER_ONLY_SIZE;

    upipe_netmap_sink->seqnum++;
    upipe_netmap_sink->seqnum &= UINT16_MAX;

    return HBRMT_HEADER_ONLY_SIZE;
}

static int upipe_put_rfc4175_headers(struct upipe_netmap_sink *upipe_netmap_sink, uint8_t *buf,
                                     uint16_t len, uint8_t field_id, uint16_t line_number,
                                     uint8_t continuation, uint16_t offset)
{
    memset(buf, 0, RFC_4175_HEADER_LEN);
    rfc4175_set_line_length(buf, len);
    rfc4175_set_line_field_id(buf, field_id);
    rfc4175_set_line_number(buf, line_number);
    rfc4175_set_line_continuation(buf, continuation);
    rfc4175_set_line_offset(buf, offset);

    return RFC_4175_HEADER_LEN;
}

/* IN: zero-indexed separated fields, OUT: zero-index interleaved fields */
static inline int get_interleaved_line(int line_number)
{
    assert(line_number < 1080);
    if (line_number >= 540)
        return (line_number - 540) * 2 + 1;

    return line_number*2;
}

/* returns 1 if uref exhausted */
static int worker_rfc4175(struct upipe *upipe, uint8_t **dst, uint16_t *len)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    uint16_t eth_frame_len = ETHERNET_HEADER_LEN + UDP_HEADER_SIZE + IP_HEADER_MINSIZE + RTP_HEADER_SIZE + RFC_4175_HEADER_LEN + RFC_4175_EXT_SEQ_NUM_LEN;
    uint16_t bytes_available = (1500 - eth_frame_len);
    uint16_t pixels1 = (((bytes_available / UPIPE_RFC4175_BLOCK_SIZE) * UPIPE_RFC4175_BLOCK_SIZE) / UPIPE_RFC4175_PIXEL_PAIR_BYTES) * 2;
    uint16_t pixels2 = 0;
    uint8_t marker = 0, continuation = 0;

    /* FIXME: hardcoded 1080 */
#define WIDTH  1920
#define HEIGHT 1080
    uint8_t field = upipe_netmap_sink->line >= HEIGHT / 2;

    /* Going to write a second partial line so limit data_len */
    if (upipe_netmap_sink->pixel_offset + pixels1 > WIDTH) {
        if (upipe_netmap_sink->line+1 == (HEIGHT/2) || upipe_netmap_sink->line+1 == HEIGHT)
            marker = 1;
        else
            continuation = 1;

        pixels1 = WIDTH - upipe_netmap_sink->pixel_offset;
    }

    uint16_t data_len1 = (pixels1 / 2) * UPIPE_RFC4175_PIXEL_PAIR_BYTES;
    uint16_t data_len2 = 0;

    eth_frame_len += data_len1;

    if (continuation) {
        bytes_available = 1500 - (eth_frame_len + RFC_4175_HEADER_LEN);
        pixels2 = (((bytes_available / UPIPE_RFC4175_BLOCK_SIZE) * UPIPE_RFC4175_BLOCK_SIZE) / UPIPE_RFC4175_PIXEL_PAIR_BYTES) * 2;
        data_len2 = (pixels2 / 2) * UPIPE_RFC4175_PIXEL_PAIR_BYTES;

        eth_frame_len += RFC_4175_HEADER_LEN;
        eth_frame_len += data_len2;

        /** XXX check for zero case */
    }
#undef WIDTH
#undef HEIGHT

    uint16_t payload_size = eth_frame_len - ETHERNET_HEADER_LEN - UDP_HEADER_SIZE - IP_HEADER_MINSIZE;

    *dst += upipe_netmap_put_headers(upipe_netmap_sink, *dst, payload_size, 103, marker);
    rfc4175_set_extended_sequence_number(*dst, (upipe_netmap_sink->seqnum >> 16) & UINT16_MAX);
    upipe_netmap_sink->seqnum++;
    upipe_netmap_sink->seqnum &= UINT32_MAX;
    *dst += RFC_4175_EXT_SEQ_NUM_LEN;
    *dst += upipe_put_rfc4175_headers(upipe_netmap_sink, *dst, data_len1, field, upipe_netmap_sink->line+1, continuation,
            upipe_netmap_sink->pixel_offset);

    if (data_len2) {
        /* Guaranteed to be from same field so continuation 0
         * Guaranteed to also start from offset 0
         */
        *dst += upipe_put_rfc4175_headers(upipe_netmap_sink, *dst, data_len2, field, upipe_netmap_sink->line+1+1, 0, 0);
    }

    int interleaved_line = get_interleaved_line(upipe_netmap_sink->line);
    if (upipe_netmap_sink->input_is_v210) {
        const uint8_t *src = upipe_netmap_sink->pixel_buffers[0] +
            upipe_netmap_sink->strides[0]*interleaved_line;

        int block_offset = upipe_netmap_sink->pixel_offset / upipe_netmap_sink->output_pixels_per_block;
        src += block_offset * upipe_netmap_sink->output_block_size;

        upipe_netmap_sink->unpack_v210((uint32_t*)src, *dst, pixels1);
    }
    else if (upipe_netmap_sink->input_bit_depth == 8) {
        const uint8_t *y8, *u8, *v8;
        y8 = upipe_netmap_sink->pixel_buffers[0] +
            upipe_netmap_sink->strides[0] * interleaved_line +
            upipe_netmap_sink->pixel_offset / 1;
        u8 = upipe_netmap_sink->pixel_buffers[1] +
            upipe_netmap_sink->strides[1] * interleaved_line +
            upipe_netmap_sink->pixel_offset / 2;
        v8 = upipe_netmap_sink->pixel_buffers[2] +
            upipe_netmap_sink->strides[2] * interleaved_line +
            upipe_netmap_sink->pixel_offset / 2;
        upipe_netmap_sink->pack_8_planar(y8, u8, v8, *dst, pixels1);
    }

    upipe_netmap_sink->pixel_offset += pixels1;
    *dst += data_len1;

    if (continuation || marker) {
        upipe_netmap_sink->pixel_offset = 0;
        if (continuation || (marker && !field)) {
            upipe_netmap_sink->line++;
        }
    }

    if (data_len2) {
        interleaved_line = get_interleaved_line(upipe_netmap_sink->line);
        //printf("\n line %i \n", interleaved_line);
        if (upipe_netmap_sink->input_is_v210) {
            const uint8_t *src = upipe_netmap_sink->pixel_buffers[0] +
                upipe_netmap_sink->strides[0]*interleaved_line;

            int block_offset = upipe_netmap_sink->pixel_offset /
                upipe_netmap_sink->output_pixels_per_block;
            src += block_offset * upipe_netmap_sink->output_block_size;

            upipe_netmap_sink->unpack_v210((uint32_t*)src, *dst, pixels2);
        }
        else if (upipe_netmap_sink->input_bit_depth == 8) {
            const uint8_t *y8, *u8, *v8;
            y8 = upipe_netmap_sink->pixel_buffers[0] +
                upipe_netmap_sink->strides[0] * interleaved_line +
                upipe_netmap_sink->pixel_offset / 1;
            u8 = upipe_netmap_sink->pixel_buffers[1] +
                upipe_netmap_sink->strides[1] * interleaved_line +
                upipe_netmap_sink->pixel_offset / 2;
            v8 = upipe_netmap_sink->pixel_buffers[2] +
                upipe_netmap_sink->strides[2] * interleaved_line +
                upipe_netmap_sink->pixel_offset / 2;
            upipe_netmap_sink->pack_8_planar(y8, u8, v8, *dst, pixels2);
        }

        upipe_netmap_sink->pixel_offset += pixels2;
    }

    *len = eth_frame_len;

    /* Release consumed frame */
    if (marker && field) {
        upipe_netmap_sink->line = 0;
        upipe_netmap_sink->pixel_offset = 0;
        upipe_netmap_sink->frame_count++;
        return 1;
    }

    return 0;
}

static int worker_hbrmt(struct upipe *upipe, uint8_t **dst, const uint8_t *src,
        int bytes_left, uint16_t *len)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    /* Enough data to fill entire ring buffer? */
    int payload_len = *len = HBRMT_DATA_SIZE;
    if (payload_len > bytes_left) {
        payload_len = bytes_left;
        /* padding */
        memset(&dst[payload_len], 0, HBRMT_DATA_SIZE - payload_len);
    }

    uint16_t payload_size = HBRMT_LEN - ETHERNET_HEADER_LEN - UDP_HEADER_SIZE -
        IP_HEADER_MINSIZE;

    /* Put headers and the marker if we've depleted the entire ubuf/frame */
    *dst += upipe_netmap_put_headers(upipe_netmap_sink, *dst, payload_size, 98, !bytes_left);
    *dst += upipe_put_hbrmt_headers(upipe_netmap_sink, *dst);

    /* Put data */
    memcpy(*dst, src, payload_len);

    return payload_len;
}

static void upipe_netmap_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    /* Source */
    struct uref *uref = upipe_netmap_sink->uref;
    const uint8_t *src_buf = NULL;
    int input_size = -1;
    int bytes_left = 0;

    /* Open up transmission ring */
    struct netmap_ring *txring = NETMAP_TXRING(upipe_netmap_sink->d->nifp,
                                               upipe_netmap_sink->ring_idx);
    uint32_t cur = txring->cur;
    uint32_t txavail = nm_ring_space(txring);
    bool rfc4175 = upipe_netmap_sink->rfc4175;

    if (rfc4175) {
        for (int i = 0; i < UPIPE_RFC4175_MAX_PLANES; i++) {
            const char *chroma = upipe_netmap_sink->input_chroma_map[i];
            if (!chroma)
                break;
            if (unlikely(!ubase_check(uref_pic_plane_read(uref, chroma,
                                0, 0, -1, -1,
                                &upipe_netmap_sink->pixel_buffers[i])) ||
                        !ubase_check(uref_pic_plane_size(uref, chroma,
                                &upipe_netmap_sink->strides[i], NULL, NULL,
                                NULL)))) {
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref_free(uref);
                upipe_netmap_sink->uref = NULL;
                return;
            }
        }
    }

    while (txavail) {
        uint8_t *dst = (uint8_t*)NETMAP_BUF(txring, txring->slot[cur].buf_idx);
        if (!uref) {
            struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
            if (!uchain)
                break;
            upipe_dbg_va(upipe, "pop, urefs: %zu", --upipe_netmap_sink->n);
            uref = uref_from_uchain(uchain);
            input_size = -1;
        }

        if (!rfc4175 && input_size == -1) {
            /* Get the buffer */
            if (unlikely(uref_block_read(uref, 0, &input_size, &src_buf))) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                uref = NULL;
                break;
            }

            bytes_left = input_size;
            upipe_dbg_va(upipe, "uref start, txavail %d bytes left %d", txavail,
                    bytes_left);
        }

        if (rfc4175) {
            if (worker_rfc4175(upipe, &dst, &txring->slot[cur].len)) {
                for (int i = 0; i < UPIPE_RFC4175_MAX_PLANES; i++) {
                    const char *chroma = upipe_netmap_sink->input_chroma_map[i];
                    if (!chroma)
                        break;
                    uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
                }
                uref_free(uref);
                uref = NULL;
            }
        } else {
            int len = worker_hbrmt(upipe, &dst, src_buf, bytes_left, &txring->slot[cur].len);
            dst += len;
            src_buf += len;
            bytes_left -= len;
        }

        cur = nm_ring_next(txring, cur);

        if (--txavail == 0) {
            // FIXME : should we really refresh TX until we exhausted all urefs?
            txring->head = txring->cur = cur;
            ioctl(NETMAP_FD(upipe_netmap_sink->d), NIOCTXSYNC, NULL);

            txavail = nm_ring_space(txring);
        }

        if (!rfc4175 && !bytes_left) {
            uref_block_unmap(uref, 0);
            uref_free(uref);
            uref = NULL;
            upipe_netmap_sink->frame_count++;
        }
    }

    if (!rfc4175 && input_size != -1) {
        upipe_dbg_va(upipe, "loop done, input size %d bytes left %d -> %d",
                input_size, bytes_left, input_size - bytes_left);
    }

    txring->head = txring->cur = cur;
    ioctl(NETMAP_FD(upipe_netmap_sink->d), NIOCTXSYNC, NULL);

    if (uref) {
        if (!rfc4175) {
            if (bytes_left > 0) {
                uref_block_unmap(uref, 0);
                uref_block_resize(uref, input_size - bytes_left, -1);
            }
        } else for (int i = 0; i < UPIPE_RFC4175_MAX_PLANES; i++) {
            const char *chroma = upipe_netmap_sink->input_chroma_map[i];
            if (!chroma)
                break;
            uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
        }
    }

    upipe_netmap_sink->uref = uref;
}

/** @internal @This outputs data to the netmap sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_netmap_sink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);
    const char *def;

    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uref_free(uref);
        return true;
    }

    ulist_add(&upipe_netmap_sink->sink_queue, uref_to_uchain(uref));
    upipe_dbg_va(upipe, "push, urefs: %zu", ++upipe_netmap_sink->n);

    return true;
}


/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_netmap_sink_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    if (!upipe_netmap_sink_check_input(upipe)) {
        upipe_netmap_sink_hold_input(upipe, uref);
        upipe_netmap_sink_block_input(upipe, upump_p);
    } else if (!upipe_netmap_sink_output(upipe, uref, upump_p)) {
        upipe_netmap_sink_hold_input(upipe, uref);
        upipe_netmap_sink_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static int upipe_netmap_sink_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);

    /* Input is V210/Planar */
    if (ubase_check(uref_flow_match_def(flow_def, "pic."))) {
        upipe_netmap_sink->rfc4175 = 1;
        uint8_t macropixel;
        if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)))
            return UBASE_ERR_INVALID;
        #define u ubase_check
        if (!(((u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
                 u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "u8")) &&
                 u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "v8"))) ||
                (u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10l")) &&
                 u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u10l")) &&
                 u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v10l"))) ||
                (u(uref_pic_flow_check_chroma(flow_def, 1, 1, 128,
                                  "u10y10v10y10u10y10v10y10u10y10v10y10")))))) {
            upipe_err(upipe, "incompatible input flow def");
            return UBASE_ERR_EXTERNAL;
        }

        upipe_netmap_sink->input_is_v210 =
            u(uref_pic_flow_check_chroma(flow_def, 1, 1, 128,
                        "u10y10v10y10u10y10v10y10u10y10v10y10"));
        upipe_netmap_sink->input_bit_depth = upipe_netmap_sink->input_is_v210
            ? 0
            : u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) ? 8 : 10;
        #undef u

        if (upipe_netmap_sink->input_is_v210) {
            upipe_netmap_sink->input_chroma_map[0] =
                "u10y10v10y10u10y10v10y10u10y10v10y10";
            upipe_netmap_sink->input_chroma_map[1] = NULL;
            upipe_netmap_sink->output_pixels_per_block = 6;
            upipe_netmap_sink->output_block_size = 16;
        }
        else if (upipe_netmap_sink->input_bit_depth == 8) {
            upipe_netmap_sink->input_chroma_map[0] = "y8";
            upipe_netmap_sink->input_chroma_map[1] = "u8";
            upipe_netmap_sink->input_chroma_map[2] = "v8";
        }
        else if (upipe_netmap_sink->input_bit_depth == 10) {
            upipe_netmap_sink->input_chroma_map[0] = "y10l";
            upipe_netmap_sink->input_chroma_map[1] = "u10l";
            upipe_netmap_sink->input_chroma_map[2] = "v10l";
        }
    } else {
        upipe_netmap_sink->rfc4175 = 0;
    }

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    uref_flow_set_def(flow_def, "block.");
    upipe_input(upipe, flow_def, NULL);

    return UBASE_ERR_NONE;
}

/** @internal @This returns the uri of the currently opened socket.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the socket
 * @return an error code
 */
static int _upipe_netmap_sink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_netmap_sink->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This is a helper for @ref _upipe_netmap_sink_set_uri.
 *
 * @param string option string
 * @return duplicated string
 */
static char *config_stropt(char *string)
{
    if (!string || !*string)
        return NULL;

    char *ret = strdup(string);
    char *tmp = ret;
    while (*tmp) {
        if (*tmp == '_')
            *tmp = ' ';
        if (*tmp == '?') {
            *tmp = '\0';
            break;
        }
        tmp++;
    }
    return ret;
}

/** @internal @This asks to open the given socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the socket
 * @param mode mode of opening the socket
 * @return an error code
 */
static int _upipe_netmap_sink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);
    char check[2];

    nm_close(upipe_netmap_sink->d);
    ubase_clean_str(&upipe_netmap_sink->uri);
    upipe_netmap_sink_set_upump(upipe, NULL);
    if (!upipe_netmap_sink_check_input(upipe))
        /* Release the pipe used in @ref upipe_netmap_sink_input. */
        upipe_release(upipe);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_netmap_sink_check_upump_mgr(upipe);

    sscanf(uri, "%*[^-]-%u%2c", &upipe_netmap_sink->ring_idx, check);
    if (strncmp(check, "/T", 2)) {
        upipe_err_va(upipe, "invalid netmap receive uri %s", uri);
        return UBASE_ERR_EXTERNAL;
    }

    /* parse uri parameters */
    char *ip = NULL;
    char *srcmac = NULL;
    char *dstmac = NULL;
    char *params = strchr(uri, '?');
    if (params) {
        char *paramsdup = strdup(params);
        char *token = paramsdup;
        do {
            *token++ = '\0';
#define IS_OPTION(option) (!strncasecmp(token, option, strlen(option)))
#define ARG_OPTION(option) (token + strlen(option))
            if (IS_OPTION("ip=")) {
                free(ip);
                ip = config_stropt(ARG_OPTION("ip="));
            } else if (IS_OPTION("srcmac=")) {
                free(srcmac);
                srcmac = config_stropt(ARG_OPTION("srcmac="));
            } else if (IS_OPTION("dstmac=")) {
                free(dstmac);
                dstmac = config_stropt(ARG_OPTION("dstmac="));
            }
#undef IS_OPTION
#undef ARG_OPTION
        } while ((token = strchr(token, '?')) != NULL);

        free(paramsdup);
        *params = '\0';
    }

//netmap:p514p1-2/T?ip=192.168.1.3:1000@192.168.1.1:1000?dstmac=0x0x0x0x?srcmac=0x0x0x
    if (!ip) {
        free(srcmac);
        free(dstmac);
        upipe_err(upipe, "ip address unspecified, use ?ip=dst:p@src:p");
        return UBASE_ERR_INVALID;
    }

    if (!srcmac) {
        free(ip);
        free(dstmac);
        upipe_err(upipe, "src mac address unspecified, use ?srcmac=0xYY:0xZZ");
        return UBASE_ERR_INVALID;
    }

    if (!dstmac) {
        free(ip);
        free(srcmac);
        upipe_err(upipe, "dst mac address unspecified, use ?dstmac=0xYY:0xZZ");
        return UBASE_ERR_INVALID;
    }

    upipe_netmap_sink->src_port = upipe_netmap_sink->ring_idx * 1000;
    upipe_netmap_sink->dst_port = upipe_netmap_sink->src_port;

    char *src_ip = strchr(ip, '@');
    if (!src_ip) {
        free(ip);
        free(dstmac);
        free(srcmac);
        upipe_err(upipe, "ip syntax incorrect");
        return UBASE_ERR_INVALID;
    }

    *src_ip++ = '\0';

    char *port = strchr(ip, ':'); // TODO: ipv6
    if (port) {
        *port++ = '\0';
        upipe_netmap_sink->dst_port = atoi(port);
    }

    port = strchr(src_ip, ':'); // TODO: ipv6
    if (port) {
        *port++ = '\0';
        upipe_netmap_sink->src_port = atoi(port);
    }

    if (sscanf(dstmac, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                &upipe_netmap_sink->dst_mac[0],
                &upipe_netmap_sink->dst_mac[1],
                &upipe_netmap_sink->dst_mac[2],
                &upipe_netmap_sink->dst_mac[3],
                &upipe_netmap_sink->dst_mac[4],
                &upipe_netmap_sink->dst_mac[5]) != 6) {
        free(ip);
        free(dstmac);
        free(srcmac);
        upipe_err(upipe, "invalid dst macaddr");
        return UBASE_ERR_INVALID;
    }

    if (sscanf(srcmac, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                &upipe_netmap_sink->src_mac[0],
                &upipe_netmap_sink->src_mac[1],
                &upipe_netmap_sink->src_mac[2],
                &upipe_netmap_sink->src_mac[3],
                &upipe_netmap_sink->src_mac[4],
                &upipe_netmap_sink->src_mac[5]) != 6) {
        free(ip);
        free(dstmac);
        free(srcmac);
        upipe_err(upipe, "invalid src macaddr");
        return UBASE_ERR_INVALID;
    }

    upipe_netmap_sink->d = nm_open(uri, NULL, 0, 0);
    if (unlikely(!upipe_netmap_sink->d)) {
        upipe_err_va(upipe, "can't open netmap socket %s", uri);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_netmap_sink->uri = strdup(uri);
    if (unlikely(upipe_netmap_sink->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }


    if (upipe_netmap_sink->d && NETMAP_FD(upipe_netmap_sink->d) != -1 &&
            upipe_netmap_sink->upump == NULL) {
        struct upump *upump;

        upump = upump_alloc_timer(upipe_netmap_sink->upump_mgr,
                upipe_netmap_sink_worker, upipe, upipe->refcount, 0,
                UCLOCK_FREQ/1000);

        upipe_netmap_sink_set_upump(upipe, upump);
        upump_start(upump);
    }

    upipe_notice_va(upipe, "opening netmap socket %s", upipe_netmap_sink->uri);
    if (!upipe_netmap_sink_check_input(upipe))
        /* Use again the pipe that we previously released. */
        upipe_use(upipe);

    upipe_netmap_sink->src_ip = inet_addr(src_ip);
    upipe_netmap_sink->dst_ip = inet_addr(ip);

    // TODO : clean strdup'd options in all error paths
    free(ip);
    free(dstmac);
    free(srcmac);

    return UBASE_ERR_NONE;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_netmap_sink_flush(struct upipe *upipe)
{
    if (upipe_netmap_sink_flush_input(upipe)) {
        upipe_netmap_sink_set_upump(upipe, NULL);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_netmap_sink_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a netmap sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_netmap_sink_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_netmap_sink_set_upump(upipe, NULL);
            return upipe_netmap_sink_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_netmap_sink_set_upump(upipe, NULL);
            upipe_netmap_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_netmap_sink_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return _upipe_netmap_sink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return _upipe_netmap_sink_set_uri(upipe, uri);
        }

        case UPIPE_FLUSH:
            return upipe_netmap_sink_flush(upipe);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a netmap sink pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_netmap_sink_control(struct upipe *upipe, int command,
        va_list args)
{
    UBASE_RETURN(_upipe_netmap_sink_control(upipe, command, args));

    // FIXME check status
#if 0
    if (unlikely(!upipe_netmap_sink_check_input(upipe)))
        upipe_netmap_sink_poll(upipe);
#endif

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_netmap_sink_free(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);
    upipe_throw_dead(upipe);

    free(upipe_netmap_sink->uri);
    upipe_netmap_sink_clean_uclock(upipe);
    upipe_netmap_sink_clean_upump(upipe);
    upipe_netmap_sink_clean_upump_mgr(upipe);
    upipe_netmap_sink_clean_input(upipe);
    upipe_netmap_sink_clean_urefcount(upipe);
    upipe_netmap_sink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_netmap_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NETMAP_SINK_SIGNATURE,

    .upipe_alloc = upipe_netmap_sink_alloc,
    .upipe_input = upipe_netmap_sink_input,
    .upipe_control = upipe_netmap_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all netmap sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_netmap_sink_mgr_alloc(void)
{
    return &upipe_netmap_sink_mgr;
}

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

#include <config.h>

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uclock_ptp.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-netmap/upipe_netmap_sink.h>

#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <pthread.h>
#include <limits.h>

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

#include "../upipe-hbrmt/sdienc.h"
#include "../upipe-hbrmt/rfc4175_enc.h"

#define UPIPE_RFC4175_MAX_PLANES 3
#define UPIPE_RFC4175_PIXEL_PAIR_BYTES 5

/* the maximum ever delay between 2 TX buffers refill */
#define NETMAP_SINK_LATENCY (UCLOCK_FREQ / 25)

/** @hidden */
static bool upipe_netmap_sink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);

struct upipe_netmap_intf {
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

    /** tx_maxrate sysctl uri */
    char *maxrate_uri;

    /** netmap descriptor **/
    struct nm_desc *d;

    int fd;
    struct ifreq ifr;

    /** packet headers */
    // TODO: rfc
    uint8_t header[ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE +
        RTP_HEADER_SIZE + HBRMT_HEADER_SIZE];

    /** if interface is up */
    bool up;

    /** time at which intf came back up */
    uint64_t wait;
};

/** @internal @This is the private context of a netmap sink pipe. */
struct upipe_netmap_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** input flow def */
    struct uref *flow_def;
    struct urational fps;

    /** frame size */
    uint64_t frame_size;
    uint64_t packets_per_frame;
    uint64_t packet_duration;
    uint64_t frame_duration;
    unsigned packet_size;
    bool progressive;

    /* Determined by the input flow_def */
    bool rfc4175;
    int input_bit_depth;
    bool input_is_v210;

    unsigned gap_fakes_current;
    unsigned gap_fakes;
    uint64_t phase_delay;
    uint64_t rtp_timestamp;

    /** picture size */
    uint64_t hsize;
    uint64_t vsize;

    /** sequence number **/
    uint64_t seqnum;
    uint64_t frame_count;

    //hbrmt header
    uint8_t frate;
    uint8_t frame;

    /** tr-03 stuff */
    int line; /* zero-indexed for consistency with below */
    int pixel_offset;
    uint64_t payload;

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
    void (*unpack_v210)(const uint32_t *src, uint8_t *dst, uintptr_t width);

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;

    int pkt;

    /** uref sink queue **/
    struct uchain sink_queue;
    size_t n;

    uint64_t bits;
    uint64_t start;

    uint64_t fakes;
    uint64_t step;
    int64_t needed_fakes;
    uint64_t pkts_in_frame;
    float pid_last_error;
    float pid_error_sum;
    float pid_last_output;
    uint64_t frame_ts;
    uint32_t prev_marker_seq;

    /** currently used uref */
    struct uref *uref;

    /** latency */
    uint64_t latency;

    /** uri */
    char *uri;

    /** prerolling */
    bool preroll;

    /* TODO: 4 for ssse3 / avx, 8 for avx2 */
#define PACK10_LOOP_SIZE 8 /* pixels per loop */

    /** packing */
    void (*pack)(uint8_t *dst, const uint8_t *y, uintptr_t pixels);

    /** packing */
    void (*pack2)(uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels);
    void (*pack2_8_planar)(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
    void (*pack2_10_planar)(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);

    /** cached packed pixels */
    uint8_t packed_pixels[PACK10_LOOP_SIZE * 5 / 2 - 1];
    /** number of cached packed pixels */
    uint8_t packed_bytes;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** rate * fps.num */
    uint64_t rate;

    struct upipe_netmap_intf intf[2];

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_netmap_sink, upipe, UPIPE_NETMAP_SINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_netmap_sink, urefcount, upipe_netmap_sink_free)
UPIPE_HELPER_UCLOCK(upipe_netmap_sink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)
UPIPE_HELPER_UPUMP_MGR(upipe_netmap_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_netmap_sink, upump, upump_mgr)

/* Compute the checksum of the given ip header. */
static uint16_t ip_checksum(const void *data, uint16_t len)
{
    const uint8_t *addr = data;
    uint32_t i;
    uint32_t sum = 0;

    /* Checksum all the pairs of bytes first... */
    for (i = 0; i < (len & ~1U); i += 2) {
        sum += (u_int16_t)ntohs(*((u_int16_t *)(addr + i)));
        if (sum > 0xFFFF)
            sum -= 0xFFFF;
    }
    /*
     * If there's a single byte left over, checksum it, too.
     * Network byte order is big-endian, so the remaining byte is
     * the high byte.
     */
    if (i < len) {
        sum += addr[i] << 8;
        if (sum > 0xFFFF)
            sum -= 0xFFFF;
    }
    return ~sum & 0xffff;
}

static void upipe_udp_raw_fill_headers(uint8_t *header,
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

    /* update ip checksum */
    ip_set_cksum(header, ip_checksum(header, IP_HEADER_MINSIZE));

    header += IP_HEADER_MINSIZE;
    udp_set_srcport(header, portsrc);
    udp_set_dstport(header, portdst);
    udp_set_len(header, len + UDP_HEADER_SIZE);
    udp_set_cksum(header, 0);
}

/* get MAC and/or IP address of specified interface */
static bool source_addr(const char *intf, uint8_t *mac, in_addr_t *ip)
{
    struct ifaddrs *ifaphead;
    if (getifaddrs(&ifaphead) != 0)
        return false;

    bool got_mac = !mac;
    bool got_ip = !ip;

    for (struct ifaddrs *ifap = ifaphead; ifap; ifap = ifap->ifa_next) {
        if (!ifap->ifa_addr)
            continue;

        if (strncmp(ifap->ifa_name, intf, IFNAMSIZ) != 0)
            continue;

        switch (ifap->ifa_addr->sa_family) {
        case AF_PACKET: /* interface mac address */
            if (mac) {
                struct sockaddr_ll *sll = (struct sockaddr_ll *)ifap->ifa_addr;
                memcpy(mac, sll->sll_addr, 6);
                got_mac = true;
            }
            break;
        case AF_INET:
            if (ip) {
                struct sockaddr_in *sin = (struct sockaddr_in *)ifap->ifa_addr;
                *ip = sin->sin_addr.s_addr;
                got_ip = true;
            }
            break;
        }
    }

    freeifaddrs(ifaphead);
    return got_mac && got_ip;
}

static int upipe_netmap_sink_open_intf(struct upipe *upipe,
        struct upipe_netmap_intf *intf, const char *uri)
{
    if (sscanf(uri, "netmap:%*[^-]-%u/T", &intf->ring_idx) != 1) {
        intf->ring_idx = 0;
    }

    char *intf_addr = strdup(&uri[strlen("netmap:")]);
    *strchr(intf_addr, '-') = '\0'; /* we already matched the - in sscanf */
    strncpy(intf->ifr.ifr_name, intf_addr, IFNAMSIZ);

    if (!source_addr(intf_addr, &intf->src_mac[0],
                &intf->src_ip)) {
        upipe_err(upipe, "Could not read interface address");
        goto error;
    }

    intf->d = nm_open(uri, NULL, 0, 0);
    if (unlikely(!intf->d)) {
        upipe_err_va(upipe, "can't open netmap socket %s", uri);
        free(intf_addr);
        return UBASE_ERR_EXTERNAL;
    }
    if (intf->d->req.nr_tx_slots < 4096) {
        upipe_err_va(upipe, "Card is not giving enough slots (%u)",
                intf->d->req.nr_tx_slots);
        nm_close(intf->d);
        free(intf_addr);
        return UBASE_ERR_EXTERNAL;
    }

    if (asprintf(&intf->maxrate_uri,
                "/sys/class/net/%s/queues/tx-%d/tx_maxrate",
                intf_addr, intf->ring_idx) < 0) {
        free(intf_addr);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    free(intf_addr);

    return UBASE_ERR_NONE;

error:
    free(intf_addr);

    return UBASE_ERR_INVALID;
}

/** @internal @This asks to open the given socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the socket
 * @param mode mode of opening the socket
 * @return an error code
 */
static int upipe_netmap_sink_open_dev(struct upipe *upipe, const char *dev)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);

    if (unlikely(dev == NULL))
        return UBASE_ERR_NONE;

    dev = strdup(dev);
    if (unlikely(dev == NULL))
        return UBASE_ERR_ALLOC;

    upipe_notice_va(upipe, "opening netmap device  %s", dev);

    char *p = strchr(dev, '+');
    if (p)
        *p++ = '\0';

    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[0];
    UBASE_RETURN(upipe_netmap_sink_open_intf(upipe, intf, dev));

    if (p) {
        p[-1] = '+';
        UBASE_RETURN(upipe_netmap_sink_open_intf(upipe, intf+1, p));
    }

    free((char*)dev);

    upipe_notice_va(upipe, "opened %d netmap device(s)", p ? 2 : 1);

    return UBASE_ERR_NONE;
}

static void upipe_netmap_sink_reset_counters(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    upipe_netmap_sink->n = 0;
    upipe_netmap_sink->fakes = 0;
    upipe_netmap_sink->step = 0;
    upipe_netmap_sink->needed_fakes = 0;
    upipe_netmap_sink->pid_last_error = 0.;
    upipe_netmap_sink->pid_error_sum = 0.;
    upipe_netmap_sink->pid_last_output = 0.;
    upipe_netmap_sink->pkts_in_frame = 0;
    upipe_netmap_sink->pkt = 0;
    upipe_netmap_sink->bits = 0;
    upipe_netmap_sink->start = 0;
    upipe_netmap_sink->preroll = true;
    upipe_netmap_sink->packed_bytes = 0;
    upipe_netmap_sink->seqnum = 0;
    upipe_netmap_sink->frame_count = 0;
    upipe_netmap_sink->phase_delay = 0;
    upipe_netmap_sink->rtp_timestamp = 0;
    upipe_netmap_sink->frame_ts = 0;
}


/** @internal @This allocates a netmap sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_netmap_sink_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    if (signature != UPIPE_NETMAP_SINK_SIGNATURE)
        return NULL;

    const char *device = va_arg(args, const char *);

    struct upipe_netmap_sink *upipe_netmap_sink = malloc(sizeof(struct upipe_netmap_sink));
    if (unlikely(!upipe_netmap_sink))
        return NULL;

    struct upipe *upipe = &upipe_netmap_sink->upipe;
    upipe_init(upipe, mgr, uprobe);

    upipe_netmap_sink->flow_def = NULL;
    upipe_netmap_sink->line = 0;
    upipe_netmap_sink->pixel_offset = 0;
    upipe_netmap_sink->frame_size = 0;
    upipe_netmap_sink->packets_per_frame = 0;
    upipe_netmap_sink->packet_duration = 0;
    upipe_netmap_sink->frame_duration = 0;
    upipe_netmap_sink->packet_size = 0;
    upipe_netmap_sink->uref = NULL;
    upipe_netmap_sink_reset_counters(upipe);
    upipe_netmap_sink->gap_fakes = 0;
    upipe_netmap_sink->gap_fakes_current = 0;

    upipe_netmap_sink->uri = NULL;
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        intf->maxrate_uri = NULL;
        intf->d = NULL;
        intf->up = true;
        intf->wait = 0;
        intf->fd = socket(AF_INET, SOCK_DGRAM, 0);
        memset(&intf->ifr, 0, sizeof(intf->ifr));
    }

    if (!ubase_check(upipe_netmap_sink_open_dev(upipe, device))) {
        upipe_clean(upipe);
        free(upipe_netmap_sink);
        return NULL;
    }

    upipe_netmap_sink_init_urefcount(upipe);
    upipe_netmap_sink_init_uclock(upipe);
    upipe_netmap_sink_init_upump_mgr(upipe);
    upipe_netmap_sink_init_upump(upipe);
    ulist_init(&upipe_netmap_sink->sink_queue);

    upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_c;
    upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_c;
    upipe_netmap_sink->unpack_v210 = upipe_v210_to_sdi_c;

    upipe_netmap_sink->pack = upipe_uyvy_to_sdi_c;
    upipe_netmap_sink->pack2 = upipe_uyvy_to_sdi_2_c;
    upipe_netmap_sink->pack2_8_planar = upipe_planar_to_sdi_8_2_c;
    upipe_netmap_sink->pack2_10_planar = upipe_planar_to_sdi_10_2_c;

#if defined(HAVE_X86ASM)
#if defined(__i686__) || defined(__x86_64__)
    if (__builtin_cpu_supports("ssse3")) {
        upipe_netmap_sink->pack = upipe_uyvy_to_sdi_ssse3;
        upipe_netmap_sink->pack2 = upipe_uyvy_to_sdi_2_ssse3;
        upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_ssse3;
        upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_ssse3;
        upipe_netmap_sink->unpack_v210 = upipe_v210_to_sdi_ssse3;
        upipe_netmap_sink->pack2_8_planar = upipe_planar_to_sdi_8_2_ssse3;
        upipe_netmap_sink->pack2_10_planar = upipe_planar_to_sdi_10_2_ssse3;
    }

    if (__builtin_cpu_supports("avx")) {
        upipe_netmap_sink->pack = upipe_uyvy_to_sdi_avx;
        upipe_netmap_sink->pack2 = upipe_uyvy_to_sdi_2_avx;
        upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_avx;
        upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_avx;
        upipe_netmap_sink->unpack_v210 = upipe_v210_to_sdi_avx;
        upipe_netmap_sink->pack2_8_planar = upipe_planar_to_sdi_8_2_avx;
        upipe_netmap_sink->pack2_10_planar = upipe_planar_to_sdi_10_2_avx;
    }

    if (__builtin_cpu_supports("avx2")) {
        upipe_netmap_sink->pack = upipe_uyvy_to_sdi_avx2;
        upipe_netmap_sink->pack2 = upipe_uyvy_to_sdi_2_avx2;
    }
#endif
#endif

    upipe_throw_ready(upipe);
    return upipe;
}

static int upipe_netmap_put_rtp_headers(struct upipe *upipe, uint8_t *buf,
        uint8_t pt, bool update, bool f2)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    memset(buf, 0, RTP_HEADER_SIZE);
    rtp_set_hdr(buf);
    rtp_set_type(buf, pt);

    if (update) {
        rtp_set_seqnum(buf, upipe_netmap_sink->seqnum & UINT16_MAX);

        const struct urational *fps = &upipe_netmap_sink->fps;
        uint64_t timestamp;
        if (upipe_netmap_sink->rfc4175) {
            __uint128_t t = upipe_netmap_sink->frame_count;
            t *= 90000;
            if (f2)
                t += 90000 / 2;
            t *= fps->den;
            t /= fps->num;
            timestamp = t;
        } else {
            timestamp = upipe_netmap_sink->frame_count * upipe_netmap_sink->frame_duration +
                (upipe_netmap_sink->frame_duration * upipe_netmap_sink->pkt * HBRMT_DATA_SIZE) /
                upipe_netmap_sink->frame_size;
            timestamp = upipe_netmap_sink->rtp_timestamp;
        }
        rtp_set_timestamp(buf, timestamp & UINT32_MAX);
    }

    return RTP_HEADER_SIZE;
}

static int upipe_netmap_put_ip_headers(struct upipe_netmap_intf *intf,
        uint8_t *buf, uint16_t payload_size)
{
    /* Destination MAC */
    ethernet_set_dstaddr(buf, intf->dst_mac);

    /* Source MAC */
    ethernet_set_srcaddr(buf, intf->src_mac);

    /* Ethertype */
    ethernet_set_lentype(buf, ETHERNET_TYPE_IP);

    buf += ETHERNET_HEADER_LEN;

    /* 0x1c - Standard, low delay, high throughput, high reliability TOS */
    upipe_udp_raw_fill_headers(buf, intf->src_ip,
                               intf->dst_ip,
                               intf->src_port,
                               intf->dst_port,
                               10, 0x1c, payload_size);

    return ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE;
}

static int upipe_put_hbrmt_headers(struct upipe *upipe, uint8_t *buf)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    memset(buf, 0, HBRMT_HEADER_SIZE);
    smpte_hbrmt_set_ext(buf, 0);
    smpte_hbrmt_set_video_source_format(buf);
    smpte_hbrmt_set_video_source_id(buf, 0);
    smpte_hbrmt_set_frame_count(buf, upipe_netmap_sink->frame_count & UINT8_MAX);
    smpte_hbrmt_set_reference_for_time_stamp(buf, 0);
    smpte_hbrmt_set_video_payload_scrambling(buf, 0);
    smpte_hbrmt_set_fec(buf, 0);
    smpte_hbrmt_set_clock_frequency(buf, 0);
    smpte_hbrmt_set_map(buf, 0);
    smpte_hbrmt_set_frame(buf, upipe_netmap_sink->frame);
    smpte_hbrmt_set_frate(buf, upipe_netmap_sink->frate);
    smpte_hbrmt_set_sample(buf, 0x1); // 422 10 bits
    smpte_hbrmt_set_fmt_reserve(buf);

    return HBRMT_HEADER_SIZE;
}

static int upipe_put_rfc4175_headers(struct upipe_netmap_sink *upipe_netmap_sink, uint8_t *buf,
                                     uint16_t len, uint8_t field_id, uint16_t line_number,
                                     uint8_t continuation, uint16_t offset)
{
    if (field_id)
        line_number -= upipe_netmap_sink->vsize / 2;
    memset(buf, 0, RFC_4175_HEADER_LEN);
    rfc4175_set_line_length(buf, len);
    rfc4175_set_line_field_id(buf, field_id);
    rfc4175_set_line_number(buf, line_number);
    rfc4175_set_line_continuation(buf, continuation);
    rfc4175_set_line_offset(buf, offset);

    return RFC_4175_HEADER_LEN;
}

static inline int get_interleaved_line(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    uint64_t vsize = upipe_netmap_sink->vsize;
    int line = upipe_netmap_sink->line;
    if (upipe_netmap_sink->progressive)
        return line;

    if (line >= vsize / 2) {
        assert(line < vsize);
        line -= vsize / 2;
        return line * 2 + 1;
    }

    return line * 2;
}

/* returns 1 if uref exhausted */
static int worker_rfc4175(struct upipe *upipe, uint8_t **dst, uint16_t **len)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    bool progressive = upipe_netmap_sink->progressive;
    bool copy = dst[1] != NULL && dst[0] != NULL;
    int idx = (dst[0] != NULL) ? 0 : 1;

    const uint16_t header_size = ETHERNET_HEADER_LEN + UDP_HEADER_SIZE + IP_HEADER_MINSIZE;
    uint16_t eth_frame_len = header_size + RTP_HEADER_SIZE + RFC_4175_HEADER_LEN + RFC_4175_EXT_SEQ_NUM_LEN;
    uint16_t pixels1 = upipe_netmap_sink->payload * 2 / UPIPE_RFC4175_PIXEL_PAIR_BYTES;

    uint8_t marker = 0, continuation = 0;

    uint8_t field = upipe_netmap_sink->line >= upipe_netmap_sink->vsize / 2;
    if (progressive)
        field = 0;

    /* End of the line */
    if (upipe_netmap_sink->pixel_offset + pixels1 >= upipe_netmap_sink->hsize) {
        /* End of the field or frame */
        if ((!progressive && upipe_netmap_sink->line+1 == (upipe_netmap_sink->vsize/2)) ||
                upipe_netmap_sink->line+1 == upipe_netmap_sink->vsize)
            marker = 1;

        pixels1 = upipe_netmap_sink->hsize - upipe_netmap_sink->pixel_offset;
    }

    uint16_t data_len1 = (pixels1 / 2) * UPIPE_RFC4175_PIXEL_PAIR_BYTES;
    eth_frame_len += data_len1;

    // FIXME -7
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d || !intf->up)
            continue;

        memcpy(dst[i], intf->header, header_size);
        dst[i] += header_size;

        /* RTP HEADER */
        int rtp_size = upipe_netmap_put_rtp_headers(upipe, dst[i], 96, true, field);
        if (marker)
            rtp_set_marker(dst[i]);
        dst[i] += rtp_size;

        rfc4175_set_extended_sequence_number(dst[i], (upipe_netmap_sink->seqnum >> 16) & UINT16_MAX);
        dst[i] += RFC_4175_EXT_SEQ_NUM_LEN;
        dst[i] += upipe_put_rfc4175_headers(upipe_netmap_sink, dst[i], data_len1, field, upipe_netmap_sink->line, continuation,
                  upipe_netmap_sink->pixel_offset);
    }

    upipe_netmap_sink->pkt++;
    upipe_netmap_sink->seqnum++;
    upipe_netmap_sink->seqnum &= UINT32_MAX;

    int interleaved_line = get_interleaved_line(upipe);
    if (upipe_netmap_sink->input_is_v210) {
        const uint8_t *src = upipe_netmap_sink->pixel_buffers[0] +
            upipe_netmap_sink->strides[0]*interleaved_line;

        int block_offset = upipe_netmap_sink->pixel_offset / upipe_netmap_sink->output_pixels_per_block;
        src += block_offset * upipe_netmap_sink->output_block_size;

        upipe_netmap_sink->unpack_v210((uint32_t*)src, dst[idx], pixels1);
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
        upipe_netmap_sink->pack_8_planar(y8, u8, v8, dst[idx], pixels1);
    }
    else if (upipe_netmap_sink->input_bit_depth == 10) {
        const uint16_t *y10, *u10, *v10;
        y10 = (uint16_t*)upipe_netmap_sink->pixel_buffers[0] +
             upipe_netmap_sink->strides[0] * interleaved_line +
             upipe_netmap_sink->pixel_offset / 1;
        u10 = (uint16_t*)upipe_netmap_sink->pixel_buffers[1] +
             upipe_netmap_sink->strides[1] * interleaved_line +
             upipe_netmap_sink->pixel_offset / 2;
        v10 = (uint16_t*)upipe_netmap_sink->pixel_buffers[2] +
             upipe_netmap_sink->strides[2] * interleaved_line +
             upipe_netmap_sink->pixel_offset / 2;
        upipe_netmap_sink->pack_10_planar(y10, u10, v10, dst[idx], pixels1);
    }

    upipe_netmap_sink->pixel_offset += pixels1;
    if (upipe_netmap_sink->pixel_offset >= upipe_netmap_sink->hsize) {
        upipe_netmap_sink->pixel_offset = 0;
        continuation = 1;
    }

    *dst += data_len1;

    /* End of the line or end of the field or frame */
    if (continuation || marker) {
        upipe_netmap_sink->pixel_offset = 0;
        if (continuation || (marker && !field)) {
            upipe_netmap_sink->line++;
        }
    }

    /* packet size */
    if (len[idx])
        *len[idx] = eth_frame_len;
    if (copy)
        *len[!idx] = *len[idx];

    upipe_netmap_sink->bits += (eth_frame_len + 4 /* CRC */) * 8;

    /* Release consumed frame */
    if (marker && (progressive || field)) {
        upipe_netmap_sink->line = 0;
        upipe_netmap_sink->pixel_offset = 0;
        upipe_netmap_sink->frame_count++;
        upipe_netmap_sink->pkt = 0;
        return 1;
    }

    return 0;
}

static int worker_hbrmt(struct upipe *upipe, uint8_t **dst, const uint8_t *src,
        int bytes_left, uint16_t **len)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    const uint8_t packed_bytes = upipe_netmap_sink->packed_bytes;
    bool copy = dst[1] != NULL && dst[0] != NULL;
    int idx = (dst[0] != NULL) ? 0 : 1;

    /* available payload size */
    int pack_bytes_left = bytes_left * 5 / 8 + packed_bytes;

    /* desired payload size */
    int payload_len = HBRMT_DATA_SIZE;
    if (payload_len > pack_bytes_left)
        payload_len = pack_bytes_left;

    /* pixels we need to read, 4 bytes per pixel */
    int pixels = ((payload_len - packed_bytes + 4)/5) * 8 / 4;
    assert(pixels);

    /* round to asm loop size */
    pixels += PACK10_LOOP_SIZE - 1;
    pixels &= ~(PACK10_LOOP_SIZE - 1);

    // FIXME: what if we exhaust the uref while outputting the penultimate packet?
    // Hi50 is fine, need to test others and eventually come up with a solution

    uint64_t timestamp = upipe_netmap_sink->frame_count * upipe_netmap_sink->frame_duration +
        (upipe_netmap_sink->frame_duration * upipe_netmap_sink->pkt++ * HBRMT_DATA_SIZE) /
        upipe_netmap_sink->frame_size;

    /* we might be trying to read more than available */
    bool end = bytes_left <= pixels * 4;

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d || !intf->up)
            continue;

        uint8_t *header = &intf->header[0];

        /* update rtp header */
        uint8_t *rtp = &header[ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE];
        rtp_set_seqnum(rtp, upipe_netmap_sink->seqnum & UINT16_MAX);
        rtp_set_timestamp(rtp, timestamp & UINT32_MAX);
        if (end)
            rtp_set_marker(rtp);

        /* copy header */
        memcpy(dst[i], header, sizeof(upipe_netmap_sink->intf[i].header));
        dst[i] += sizeof(upipe_netmap_sink->intf[i].header);

        /* unset rtp marker if needed */
        if (end)
            rtp_clear_marker(rtp);

        /* use previous scratch buffer */
        if (packed_bytes) {
            memcpy(dst[i], upipe_netmap_sink->packed_pixels, packed_bytes);
            dst[i] += packed_bytes;
        }
    }

    upipe_netmap_sink->seqnum++;

    /* convert pixels */
    if (copy)
        upipe_netmap_sink->pack2(dst[0], dst[1], src, pixels);
    else if (dst[idx])
        upipe_netmap_sink->pack(dst[idx], src, pixels);

    /* bytes these pixels decoded to */
    int bytes = pixels * 4 * 5 / 8;

    /* overlap */
    int pkt_rem = bytes - (payload_len - packed_bytes);
    assert(pkt_rem <= sizeof(upipe_netmap_sink->packed_pixels));
    if (pkt_rem > 0) {
        if (dst[idx])
            memcpy(upipe_netmap_sink->packed_pixels, dst[idx] + bytes - pkt_rem, pkt_rem);
        if (copy)
            memcpy(upipe_netmap_sink->packed_pixels, dst[1] + bytes - pkt_rem, pkt_rem);
    }

    /* update overlap count */
    upipe_netmap_sink->packed_bytes = pkt_rem;

    /* padding */
    if (payload_len != HBRMT_DATA_SIZE) {
        if (dst[idx])
            memset(dst[idx] + payload_len, 0, HBRMT_DATA_SIZE - payload_len);
        if (copy)
            memset(dst[1] + payload_len, 0, HBRMT_DATA_SIZE - payload_len);
    }

    /* packet size */
    if (len[idx])
        *len[idx] = ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE +
            RTP_HEADER_SIZE + HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE;
    if (copy)
        *len[!idx] = *len[idx];

    if (end) {
        upipe_netmap_sink->packed_bytes = 0;
        return bytes_left;
    }

    return pixels * 4;
}

static float pts_to_time(uint64_t pts)
{
    return (float)pts / 27000;
}

static void upipe_resync_queues(struct upipe *upipe, uint32_t packets)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[0];
    if (!intf->up) {
        intf = &upipe_netmap_sink->intf[1];
    }

    struct netmap_ring *txring = NETMAP_TXRING(intf->d->nifp, intf->ring_idx);

    const unsigned len = upipe_netmap_sink->packet_size;

    uint32_t cur = txring->cur;
    for (uint32_t i = 0; i < packets; i++) {
        uint8_t *dst = (uint8_t*)NETMAP_BUF(txring, txring->slot[cur].buf_idx);
        memset(dst, 0, len);
        memcpy(dst, intf->header, ETHERNET_HEADER_LEN);
        txring->slot[cur].len = len;
        cur = nm_ring_next(txring, cur);
    }
    txring->head = txring->cur = cur;
}

static struct uref *get_uref(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    struct uref *uref = upipe_netmap_sink->uref;
    struct urational *fps = &upipe_netmap_sink->fps;

    uint64_t now = uclock_now(upipe_netmap_sink->uclock);

    if (uref) {
        uint64_t pts = 0;
        uref_clock_get_pts_sys(uref, &pts);
        pts += upipe_netmap_sink->latency;
        pts += upipe_netmap_sink->phase_delay;

        if (pts + UCLOCK_FREQ * fps->den / fps->num + NETMAP_SINK_LATENCY < now) {
            uref_block_unmap(uref, 0);
            uref_free(uref);
            uref = NULL;
            upipe_warn_va(upipe, "drop late buffered frame, %" PRIu64 "ms, now %.2f pts %.2f latency %.2f",
                    (now - pts) / 27000,
                    pts_to_time(now),
                    pts_to_time(pts - upipe_netmap_sink->latency),
                    (float)upipe_netmap_sink->latency / 27000
                    );
        }
    }

    while (!uref) {
        struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
        if (!uchain)
            break;

        upipe_netmap_sink->n--;
        uref = uref_from_uchain(uchain);


        if (upipe_netmap_sink->preroll) {
            break;
        }

        uint64_t pts = 0;
        uref_clock_get_pts_sys(uref, &pts);
        pts += upipe_netmap_sink->latency;
        pts += upipe_netmap_sink->phase_delay;

        if (pts + NETMAP_SINK_LATENCY < now) {
            uref_free(uref);
            uref = NULL;
            upipe_warn_va(upipe, "drop late frame, %" PRIu64 "ms, now %.2f pts %.2f latency %.2f",
                    (now - pts) / 27000,
                    pts_to_time(now),
                    pts_to_time(pts - upipe_netmap_sink->latency),
                    (float)upipe_netmap_sink->latency / 27000
                    );
        }
    }

    upipe_netmap_sink->uref = uref;
    return uref;
}

static int compute_fakes(struct upipe *upipe, int j)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    const struct urational *fps = &upipe_netmap_sink->fps;

    if (j > 50)
        j = 50;
    if (j < -50)
        j = -50;
    float i = j;

    float error = -i;
    upipe_netmap_sink->pid_error_sum += error * fps->den / fps->num;

    /* avoid swinging too far when initial error is large */
    if (upipe_netmap_sink->pid_error_sum > 200)
        upipe_netmap_sink->pid_error_sum = 200;

    float d = (error - upipe_netmap_sink->pid_last_error) * fps->num / fps->den;
    if (fps->num / fps->den > 59)
        d /= 2;
    if (d > 300)
        d = 300;
    if (d < -300)
        d = -300;

    upipe_netmap_sink->pid_last_output = (error + upipe_netmap_sink->pid_error_sum + d) / 3;
    upipe_netmap_sink->pid_last_error = error;

    if (0)
    upipe_dbg_va(upipe, " <= %.1f" "\tP = %.1f" "\tI = %.1f" "\tD = %.1f" "\t=> %.1f",
            i, error, upipe_netmap_sink->pid_error_sum, d, upipe_netmap_sink->pid_last_output);
    return (int)upipe_netmap_sink->pid_last_output;
}

static void handle_tx_stamp(struct upipe *upipe, uint64_t t, uint16_t seq)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    const int64_t dur = UCLOCK_FREQ * upipe_netmap_sink->fps.den / upipe_netmap_sink->fps.num;
    assert(t);

    t /= 1000;
    t *= 27;

    if (upipe_netmap_sink->frame_ts == 0) {
        upipe_netmap_sink->prev_marker_seq = seq;
        upipe_netmap_sink->frame_ts = t;
        upipe_netmap_sink->frame_ts /= dur;
        upipe_netmap_sink->frame_count = upipe_netmap_sink->frame_ts + 1;
        upipe_netmap_sink->frame_ts *= dur;
        upipe_netmap_sink->phase_delay = t - upipe_netmap_sink->frame_ts;
        return;
    }

    uint16_t s = seq - upipe_netmap_sink->prev_marker_seq;
    if (s != upipe_netmap_sink->packets_per_frame) { /* we missed a marker */
        uint64_t frames = (s - 1) / upipe_netmap_sink->packets_per_frame;
        upipe_warn_va(upipe, "Missed %" PRIu64 " marker frames", frames);
        upipe_netmap_sink->frame_ts += frames * dur;
    }
    upipe_netmap_sink->prev_marker_seq = seq;

    upipe_netmap_sink->frame_ts += dur;

    int64_t x = t - upipe_netmap_sink->frame_ts;
    int64_t ideal = (dur - x) * (int64_t)upipe_netmap_sink->packets_per_frame / dur;

    int step = compute_fakes(upipe, -ideal);
    upipe_netmap_sink->needed_fakes *= 9;
    upipe_netmap_sink->needed_fakes += step;
    upipe_netmap_sink->needed_fakes /= 10;

    if (upipe_netmap_sink->needed_fakes < 0) // if we're too late, just wait till we drift back
        upipe_netmap_sink->needed_fakes = 0;

    if (upipe_netmap_sink->needed_fakes) {
        uint64_t total = upipe_netmap_sink->packets_per_frame;
        if (upipe_netmap_sink->rfc4175)
            total += upipe_netmap_sink->gap_fakes;
        upipe_netmap_sink->step = (total + upipe_netmap_sink->needed_fakes - 1) / upipe_netmap_sink->needed_fakes;
    } else
        upipe_netmap_sink->step = 0;

    upipe_dbg_va(upipe,
            "%.2f ms, ideal %" PRId64 ""
            " step %d, fakes %" PRIu64 " needed fakes %" PRIu64 "",
            (float)(dur - x) / 27000., ideal, step,
            upipe_netmap_sink->fakes, upipe_netmap_sink->needed_fakes);
    upipe_netmap_sink->fakes = 0;

    upipe_netmap_sink->pkts_in_frame = 0;
}

static void upipe_netmap_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    uint64_t now = uclock_now(upipe_netmap_sink->uclock);
    {
        static uint64_t old;
        if (old == 0)
            old = now;
        if ((now - old) > UCLOCK_FREQ / 50)
            printf("%" PRIu64 " %s() after %" PRIu64 "us\n", now, __func__, (now - old) / 27);
        old = now;
    }

    if (!upipe_netmap_sink->flow_def)
        return;

    /* Source */
    struct uref *uref = get_uref(upipe);
    const uint8_t *src_buf = NULL;
    int input_size = -1;
    int bytes_left = 0;

    if (uref && upipe_netmap_sink->preroll) {
        uint64_t pts = 0;
        uref_clock_get_pts_sys(uref, &pts);
        pts += upipe_netmap_sink->latency;
        pts += NETMAP_SINK_LATENCY;
        if (pts > now) {
            upipe_dbg_va(upipe, "preroll : pts - now = %" PRIu64 "ms",
                    (pts - now) / 27000);
            return;
        }
        upipe_notice_va(upipe, "end of preroll");
        upipe_netmap_sink->preroll = false;
    }

    struct netmap_ring *txring[2] = { NULL, NULL };
    uint32_t cur[2];
    bool up[2] = {false, false};

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d)
            break;

        txring[i] = NETMAP_TXRING(intf->d->nifp, intf->ring_idx);

        struct ifreq ifr = intf->ifr;
        if (ioctl(intf->fd, SIOCGIFFLAGS, &ifr) < 0)
            perror("ioctl");
        up[i] = ifr.ifr_flags & IFF_RUNNING;
        if (up[i] != intf->up) {
            if (!intf->wait) {
                upipe_warn_va(upipe, "LINK %zd went %s", i, up[i] ? "UP" : "DOWN");
                if (up[i])
                    intf->wait = now;
            }
            intf->up = false; /* will come up after waiting */
            if (!upipe_netmap_sink->bits) {
                intf->up = up[i];
                intf->wait = 0;
            }
        }
    }

    now = uclock_now(upipe_netmap_sink->uclock);

    uint32_t txavail = UINT32_MAX;
    uint32_t max_slots = UINT32_MAX;
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d || !up[i]) {
            txring[i] = NULL;
            continue;
        }

        uint32_t t = nm_ring_space(txring[i]);
        max_slots = txring[i]->num_slots - 1;

        if (intf->wait) {
            if ((now - intf->wait) > UCLOCK_FREQ) {
                ioctl(NETMAP_FD(intf->d), NIOCTXSYNC, NULL); // update userspace ring
                if (t < max_slots - 32) {
                    upipe_notice_va(upipe, "waiting, %u", t);
                    continue;
                }

                if (!txring[!i]) { // 2nd interface down
                    intf->up = true;
                    intf->wait = 0;
                    break;
                }

                // update other NIC
                struct upipe_netmap_intf *intf0 = &upipe_netmap_sink->intf[!i];
                ioctl(NETMAP_FD(intf0->d), NIOCTXSYNC, NULL);
                txavail = nm_ring_space(txring[!i]);

                // synchronize within 1024 packets
                upipe_resync_queues(upipe, txring[!i]->num_slots - 1 - txavail - 1024);

                // update NIC, should start outputting packets
                ioctl(NETMAP_FD(intf->d), NIOCTXSYNC, NULL);

                // update other NIC again
                ioctl(NETMAP_FD(intf0->d), NIOCTXSYNC, NULL);
                txavail = nm_ring_space(txring[!i]);
                t = nm_ring_space(txring[i]);
                upipe_notice_va(upipe, "RESYNCED (#1), tx0 %u tx1 %u", txavail, t);

                // synchronize exactly
                upipe_resync_queues(upipe, t - txavail);

                // update both NICs
                ioctl(NETMAP_FD(intf->d), NIOCTXSYNC, NULL); // start emptying 1
                ioctl(NETMAP_FD(intf0->d), NIOCTXSYNC, NULL);

                // update NIC txavail
                txavail = nm_ring_space(txring[!i]);
                t = nm_ring_space(txring[i]);

                // we're up
                intf->up = true;
                intf->wait = 0;

                upipe_notice_va(upipe, "RESYNCED (#2), tx0 %u tx1 %u", txavail, t);
            } else {
                txring[i] = NULL;
                continue;
            }
        }

        cur[i] = txring[i]->cur;

        if (txavail > t)
            txavail = t;
    }

    uint32_t num_slots = 0;
    for (size_t i = 0; i < 2; i++) {
        if (txring[i]) {
            num_slots = txring[i]->num_slots;
            break;
        }

    }

    if (!num_slots) {
        if (upipe_netmap_sink->bits)
            upipe_err(upipe, "No interface is up, reset!");
        if (uref) {
            uref_block_unmap(uref, 0);
            uref_free(uref);
        }

        for (;;) {
            struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
            if (!uchain)
                break;
            struct uref *uref = uref_from_uchain(uchain);
            uref_free(uref);
        }

        upipe_netmap_sink_reset_counters(upipe);
        upipe_netmap_sink->uref = NULL;

        return;
    }

    bool ddd = false;
    {
        static uint64_t prev;
        if (now - prev > UCLOCK_FREQ) {
            prev = now;
            ddd = true;
        }
    }

    const bool rfc4175 = upipe_netmap_sink->rfc4175;
    const unsigned pkt_len = upipe_netmap_sink->packet_size;

    if (txavail > (num_slots / 2) && !upipe_netmap_sink->n)
        ddd = true;

    __uint128_t bps = upipe_netmap_sink->bits;
    if (bps)
        bps -= (num_slots - 1 - txavail) * (pkt_len + 4) * 8;

    struct netmap_slot *slot = &txring[0]->slot[cur[0]];
    uint64_t t = slot->ptr / 1000 * 27;
    if (!t)
        t = now;

    bps *= UCLOCK_FREQ;
    bps /= t - upipe_netmap_sink->start;

    int64_t err = bps * upipe_netmap_sink->fps.den - upipe_netmap_sink->rate;
    err /= (int64_t)upipe_netmap_sink->fps.den;

    if (ddd) {
        upipe_dbg_va(upipe,
                "txavail %d at %" PRIu64 " bps -> err %" PRId64 ", %zu urefs, "
                "epoch offset", txavail, (uint64_t)bps, err, upipe_netmap_sink->n);
    }

    if (upipe_netmap_sink->start) {
        // for gnuplot
        //printf("%" PRIu64 " %" PRIu64 "\n", now - upipe_netmap_sink->start, (int64_t)bps);
    }

    bool progressive = upipe_netmap_sink->progressive;

    /* fill ring buffer */
    while (txavail) {
        if (upipe_netmap_sink->step && (upipe_netmap_sink->pkts_in_frame % upipe_netmap_sink->step) == 0) {
            const unsigned len = upipe_netmap_sink->packet_size;
            for (size_t i = 0; i < 2; i++) {
                struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
                if (!intf->d || !intf->up)
                    continue;
                uint8_t *dst = (uint8_t*)NETMAP_BUF(txring[i], txring[i]->slot[cur[i]].buf_idx);

                const size_t udp_size = ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE;
                uint8_t *rtp = &dst[udp_size];
                if (rtp_check_marker(rtp)) /* marker needs to be set */ {
                    bool stamp = false;
                    if (rfc4175) {
                        uint8_t *rfc = &rtp[RTP_HEADER_SIZE+ RFC_4175_EXT_SEQ_NUM_LEN];
                        uint8_t f2 = rfc4175_get_line_field_id(rfc);
                        if (f2 || progressive)
                            stamp = true;
                    } else {
                        stamp = true;
                    }
                    if (stamp) {
                        uint16_t seq = rtp_get_seqnum(rtp);
                        handle_tx_stamp(upipe, txring[i]->slot[cur[i]].ptr, seq);
                    }
                }
                memset(dst, 0, len);
                memcpy(dst, intf->header, ETHERNET_HEADER_LEN);
                txring[i]->slot[cur[i]].len = len;
                cur[i] = nm_ring_next(txring[i], cur[i]);
            }
            txavail--;
            upipe_netmap_sink->fakes++;
            upipe_netmap_sink->pkts_in_frame++;
            if (!txavail)
                break;
        }
        if (!uref) {
            uref = get_uref(upipe);
            if (!uref)
                break;
            input_size = -1;
        }

        if (input_size == -1) {
            if (!rfc4175) {
                /* Get the buffer */
                if (unlikely(uref_block_read(uref, 0, &input_size, &src_buf))) {
                    uref_free(uref);
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                    uref = NULL;
                    break;
                }
            } else {
                /* map picture */
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
                input_size = 1;
            }

            bytes_left = input_size;
        }

        uint8_t *dst[2] = { NULL, NULL };
        uint16_t *len[2] = { NULL, NULL };

        for (size_t i = 0; i < 2; i++) {
            struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
            if (!intf->d || !intf->up)
                continue;

            struct netmap_slot *slot = &txring[i]->slot[cur[i]];

            dst[i] = (uint8_t*)NETMAP_BUF(txring[i], slot->buf_idx);
            len[i] = &slot->len;
            cur[i] = nm_ring_next(txring[i], cur[i]);

            /* look for exact TX time of end of frame */
            if (!slot->ptr) /* no timestamp available */
                continue;

            const size_t udp_size = ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE;
            if (*len[i] < udp_size + RTP_HEADER_SIZE)
                continue;

            uint8_t *rtp = &dst[i][udp_size];
            if (!rtp_check_marker(rtp)) /* marker needs to be set */
                continue;

            if (rfc4175) {
                if (*len[i] < udp_size + RTP_HEADER_SIZE + RFC_4175_EXT_SEQ_NUM_LEN + RFC_4175_HEADER_LEN)
                    continue;
                uint8_t *rfc = &rtp[RTP_HEADER_SIZE+ RFC_4175_EXT_SEQ_NUM_LEN];
                uint8_t f2 = rfc4175_get_line_field_id(rfc);
                if (!progressive && !f2)
                    continue;
            }

            uint16_t seq = rtp_get_seqnum(rtp);
            handle_tx_stamp(upipe, slot->ptr, seq);
        }

        if (rfc4175) {
            // TODO: -7
            if ((upipe_netmap_sink->line == 0 ||
                (!progressive && upipe_netmap_sink->line == upipe_netmap_sink->vsize / 2)) && upipe_netmap_sink->pixel_offset == 0 && upipe_netmap_sink->gap_fakes_current) {

                for (size_t i = 0; i < 2; i++) {
                    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
                    if (!intf->d || !intf->up)
                        continue;
                    memset(dst[i], 0, pkt_len);
                    memcpy(dst[i], intf->header, ETHERNET_HEADER_LEN);
                    *len[i] = pkt_len;
                }
                upipe_netmap_sink->bits += (pkt_len + 4 /* CRC */) * 8;
                upipe_netmap_sink->gap_fakes_current--;
            } else {
                upipe_netmap_sink->gap_fakes_current = upipe_netmap_sink->gap_fakes;
                if (!progressive)
                    upipe_netmap_sink->gap_fakes_current /= 2;

                if (worker_rfc4175(upipe, dst, len)) {
                    for (int i = 0; i < UPIPE_RFC4175_MAX_PLANES; i++) {
                        const char *chroma = upipe_netmap_sink->input_chroma_map[i];
                        if (!chroma)
                            break;
                        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
                    }
                    uref_free(uref);
                    uref = NULL;
                    upipe_netmap_sink->uref = NULL;
                    bytes_left = 0;
                }
            }
        } else {
            int s = worker_hbrmt(upipe, dst, src_buf, bytes_left, len);
            src_buf += s;
            bytes_left -= s;
            assert(bytes_left >= 0);

            // FIXME
            uint16_t l = 1438;//len[0] ? *len[0] : *len[1];
            assert(l == 1438);

            /* 64 bits overflows after 375 years at 1.5G */
            upipe_netmap_sink->bits += (l + 4 /* CRC */) * 8;

            if (!bytes_left) {
                uref_block_unmap(uref, 0);
                uref_free(uref);
                uref = NULL;
                upipe_netmap_sink->uref = NULL;
                upipe_netmap_sink->pkt = 0;

                upipe_netmap_sink->frame_count++;
                upipe_netmap_sink->rtp_timestamp += upipe_netmap_sink->frame_duration;
                for (size_t i = 0; i < 2; i++) {
                    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
                    if (!intf->d)
                        continue;

                    /* update hbrmt header */
                    uint8_t *hbrmt = &intf->header[ETHERNET_HEADER_LEN +
                        IP_HEADER_MINSIZE + UDP_HEADER_SIZE + RTP_HEADER_SIZE];
                    smpte_hbrmt_set_frame_count(hbrmt, upipe_netmap_sink->frame_count & UINT8_MAX);
                }
            }
        }

        upipe_netmap_sink->pkts_in_frame++;
        txavail--;
    }

    if (txavail >= max_slots - 32) {
        upipe_netmap_sink_reset_counters(upipe);
        for (;;) {
            struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
            if (!uchain)
                break;
            struct uref *uref = uref_from_uchain(uchain);
            uref_free(uref);
        }
        upipe_netmap_sink->uref = NULL;
        upipe_netmap_sink_set_upump(upipe, NULL);
    }

    if (uref && bytes_left > 0) {
        if (!rfc4175) {
            /* resize current buffer */
            uref_block_unmap(uref, 0);
            uref_block_resize(uref, input_size - bytes_left, -1);
        } else for (int i = 0; i < UPIPE_RFC4175_MAX_PLANES; i++) {
            const char *chroma = upipe_netmap_sink->input_chroma_map[i];
            if (!chroma)
                break;
            uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
        }
    }

    upipe_netmap_sink->uref = uref;

    if (!upipe_netmap_sink->start && txavail < max_slots - 32)
        upipe_netmap_sink->start = uclock_now(upipe_netmap_sink->uclock);

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d || !intf->up)
            continue;

        txring[i]->head = txring[i]->cur = cur[i];
        ioctl(NETMAP_FD(intf->d), NIOCTXSYNC, NULL);
    }
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
        uref_free(upipe_netmap_sink->flow_def);
        upipe_netmap_sink->flow_def = uref;
        upipe_netmap_sink->preroll = true;
        return true;
    }

    if (upipe_netmap_sink->frame_size == 0) {
        if (!upipe_netmap_sink->rfc4175) {
            uref_block_size(uref, &upipe_netmap_sink->frame_size);
            upipe_netmap_sink->frame_size = upipe_netmap_sink->frame_size * 5 / 8;
            upipe_netmap_sink->packets_per_frame = (upipe_netmap_sink->frame_size + HBRMT_DATA_SIZE - 1) / HBRMT_DATA_SIZE;
            static const uint64_t eth_packet_size =
            ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE +
                RTP_HEADER_SIZE + HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE +
                    4 /* ethernet CRC */;

            upipe_netmap_sink->rate = 8 * eth_packet_size * upipe_netmap_sink->packets_per_frame * upipe_netmap_sink->fps.num;
        } else {
            uint64_t pixels = upipe_netmap_sink->hsize * upipe_netmap_sink->vsize;
            upipe_netmap_sink->frame_size = pixels * UPIPE_RFC4175_PIXEL_PAIR_BYTES / 2;
            const uint16_t eth_header_len = ETHERNET_HEADER_LEN + UDP_HEADER_SIZE + IP_HEADER_MINSIZE + RTP_HEADER_SIZE + RFC_4175_HEADER_LEN + RFC_4175_EXT_SEQ_NUM_LEN;
            const uint16_t bytes_available = upipe_netmap_sink->packet_size - eth_header_len;
            const uint64_t payload = (bytes_available / UPIPE_RFC4175_PIXEL_PAIR_BYTES) * UPIPE_RFC4175_PIXEL_PAIR_BYTES;
            upipe_netmap_sink->payload = payload;

            upipe_netmap_sink->packets_per_frame = (upipe_netmap_sink->frame_size + payload - 1) / payload;

            uint64_t packets = upipe_netmap_sink->packets_per_frame;
            bool progressive = upipe_netmap_sink->progressive;
            if (!progressive && upipe_netmap_sink->hsize == 720) {
                if (upipe_netmap_sink->vsize == 486) {
                    packets *= 525;
                    packets /= 487;
                } else if (upipe_netmap_sink->vsize == 576) {
                    packets *= 625;
                    packets /= 576;
                }
            } else {
                    packets *= 1125;
                    packets /= 1080;
            }

            upipe_netmap_sink->rate = 8 * (packets * (eth_header_len + payload + 4 /* CRC */)) * upipe_netmap_sink->fps.num;
        }
        upipe_netmap_sink->packet_duration = upipe_netmap_sink->frame_duration / upipe_netmap_sink->packets_per_frame;

        for (size_t i = 0; i < 2; i++) {
            struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
            if (!intf->d)
                break;
            FILE *f = fopen(intf->maxrate_uri, "w");

            struct ifreq ifr = intf->ifr;
            uint32_t u = (intf->ring_idx << 16) | (upipe_netmap_sink->packet_size + 4);
            ifr.ifr_data = (void*)&u;
            if (ioctl(intf->fd, SIOCDEVPRIVATE, &ifr) < 0)
                perror("ioctl");

            if (!f) {
                upipe_err_va(upipe, "Could not open maxrate sysctl %s",
                        intf->maxrate_uri);
            } else {
                fprintf(f, "%" PRIu64, upipe_netmap_sink->rate / upipe_netmap_sink->fps.den);
                fclose(f);
            }
        }
    }

    bool up = false;
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d)
            break;

        struct ifreq ifr = intf->ifr;
        if (ioctl(intf->fd, SIOCGIFFLAGS, &ifr) < 0)
            perror("ioctl");

        if (ifr.ifr_flags & IFF_RUNNING)
            up = true;
    }

    if (!up) {
        uref_free(uref);
        return true;
    }

    ulist_add(&upipe_netmap_sink->sink_queue, uref_to_uchain(uref));
    upipe_netmap_sink->n++;
    //upipe_notice_va(upipe, "push, urefs: %zu", upipe_netmap_sink->n);

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
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[0];

    upipe_netmap_sink_check_upump_mgr(upipe);
    if (!upipe_netmap_sink->uclock) {
        uref_free(uref);
        return;
    }

    if (upipe_netmap_sink->upump == NULL && upipe_netmap_sink->upump_mgr) {
        if (intf->d && NETMAP_FD(intf->d) != -1) {
            struct upump *upump = upump_alloc_timer(upipe_netmap_sink->upump_mgr,
                    upipe_netmap_sink_worker, upipe, upipe->refcount, 0,
                    UCLOCK_FREQ/1000);

            upipe_netmap_sink_set_upump(upipe, upump);
            upump_start(upump);
        }
    }

    upipe_netmap_sink_output(upipe, uref, upump_p);
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
                (u(uref_pic_flow_check_chroma(flow_def, 1, 1, 16,
                                  "u10y10v10y10u10y10v10y10u10y10v10y10")))))) {
            upipe_err(upipe, "incompatible input flow def");
            return UBASE_ERR_EXTERNAL;
        }

        upipe_netmap_sink->input_is_v210 =
            u(uref_pic_flow_check_chroma(flow_def, 1, 1, 16,
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
        upipe_netmap_sink->packet_size = 1438;
    }

    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &upipe_netmap_sink->hsize));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &upipe_netmap_sink->vsize));
    upipe_netmap_sink->progressive = ubase_check(uref_pic_get_progressive(flow_def));

    if (upipe_netmap_sink->hsize == 720) {
        if (upipe_netmap_sink->rfc4175)
            upipe_netmap_sink->packet_size = 962;
        if (upipe_netmap_sink->vsize == 486) {
            upipe_netmap_sink->frame = 0x10;
            if (upipe_netmap_sink->rfc4175)
                upipe_netmap_sink->gap_fakes = 2 * (525 - 486); // 2 packets per line
        } else if (upipe_netmap_sink->vsize == 576) {
            upipe_netmap_sink->frame = 0x11;
            if (upipe_netmap_sink->rfc4175)
                upipe_netmap_sink->gap_fakes = 2 * (625 - 576);
        } else
            return UBASE_ERR_INVALID;
    } else if (upipe_netmap_sink->hsize == 1920 && upipe_netmap_sink->vsize == 1080) {
        if (upipe_netmap_sink->rfc4175) {
            upipe_netmap_sink->packet_size = 1262;
            upipe_netmap_sink->gap_fakes = 4 * (1125 - 1080);
        }
        upipe_netmap_sink->frame = 0x20; // interlaced
        // FIXME: progressive/interlaced is per-picture
        // XXX: should we do PSF at all?
        // 0x21 progressive
        // 0x22 psf
    } else if (upipe_netmap_sink->hsize == 1280 && upipe_netmap_sink->vsize == 720) {
        if (upipe_netmap_sink->rfc4175) {
            upipe_netmap_sink->packet_size = 862;
            upipe_netmap_sink->gap_fakes = 4 * (750 - 720);
        }
        upipe_netmap_sink->frame = 0x30; // progressive
    } else
        return UBASE_ERR_INVALID;

    static const struct  {
        struct urational fps;
        uint8_t frate;
    } frate[] = {
        { { 60,       1 }, 0x10 },
        { { 60000, 1001 }, 0x11 },
        { { 50,       1 }, 0x12 },
        { { 48,       1 }, 0x14 },
        { { 48000, 1001 }, 0x15 },
        { { 30,       1 }, 0x16 },
        { { 30000, 1001 }, 0x17 },
        { { 25,       1 }, 0x18 },
        { { 24,       1 }, 0x1a },
        { { 24000, 1001 }, 0x1b },
    };
    UBASE_RETURN(uref_pic_flow_get_fps(flow_def, &upipe_netmap_sink->fps));
    upipe_netmap_sink->frame_duration = UCLOCK_FREQ * upipe_netmap_sink->fps.den / upipe_netmap_sink->fps.num;

    if (!ubase_check(uref_clock_get_latency(flow_def, &upipe_netmap_sink->latency))) {
        upipe_err(upipe, "no latency");
        upipe_netmap_sink->latency = 0;
    } else {
        upipe_notice_va(upipe, "latency %" PRIu64, upipe_netmap_sink->latency);
    }

    upipe_netmap_sink->frate = 0;
    for (int i = 0; i < sizeof(frate) / sizeof(frate[0]); i++) {
        if (!urational_cmp(&frate[i].fps, &upipe_netmap_sink->fps)) {
            upipe_netmap_sink->frate = frate[i].frate;
            break;
        }
    }

    if (!upipe_netmap_sink->frate)
        return UBASE_ERR_INVALID;

    if (!upipe_netmap_sink->rfc4175) {
        for (size_t i = 0; i < 2; i++) {
            struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
            if (!intf->d)
                break;
            uint8_t *header = &intf->header[0];
            const uint16_t udp_payload_size = RTP_HEADER_SIZE +
                HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE;
            header += upipe_netmap_put_ip_headers(intf, header, udp_payload_size);
            header += upipe_netmap_put_rtp_headers(upipe, header, 98, false, false);
            header += upipe_put_hbrmt_headers(upipe, header);
            assert(header == &intf->header[sizeof(intf->header)]);
        }
    } else {
        const uint16_t header_size = ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE;
        for (size_t i = 0; i < 2; i++) {
            struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
            if (!intf->d)
                break;
            uint8_t *header = &intf->header[0];
            uint16_t udp_payload_size = upipe_netmap_sink->packet_size - header_size;
            header += upipe_netmap_put_ip_headers(intf, header, udp_payload_size);
            /* RTP Headers done in worker_rfc4175 */
        }
    }

    upipe_netmap_sink->frame_size = 0;
    upipe_netmap_sink_reset_counters(upipe);
    for (;;) {
        struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
        if (!uchain)
            break;
        struct uref *uref = uref_from_uchain(uchain);
        uref_free(uref);
    }
    upipe_netmap_sink->uref = NULL;

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
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

/** @internal @This is a helper for @ref upipe_netmap_sink_set_uri.
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

static int upipe_netmap_sink_ip_params(struct upipe *upipe,
        struct upipe_netmap_intf *intf, const char *params)
{
    char *ip = NULL;
    char *dstmac = NULL;

    if (params && *params == '?') {
        char *paramsdup = strdup(params);
        char *token = paramsdup;
        do {
            *token++ = '\0';
#define IS_OPTION(option) (!strncasecmp(token, option, strlen(option)))
#define ARG_OPTION(option) (token + strlen(option))
            if (IS_OPTION("ip=")) {
                free(ip);
                ip = config_stropt(ARG_OPTION("ip="));
            } else if (IS_OPTION("dstmac=")) {
                free(dstmac);
                dstmac = config_stropt(ARG_OPTION("dstmac="));
            }
#undef IS_OPTION
#undef ARG_OPTION
        } while ((token = strchr(token, '?')) != NULL);

        free(paramsdup);
    }

    if (!ip) {
        upipe_err(upipe, "ip address unspecified, use ?ip=dst:p");
        goto error;
    }

    intf->src_port = (intf->ring_idx+1) * 1000;
    intf->dst_port = intf->src_port;

    char *port = strchr(ip, ':'); // TODO: ipv6
    if (port) {
        *port++ = '\0';
        intf->dst_port = atoi(port);
    }

    intf->dst_ip = inet_addr(ip);

    if (dstmac) {
        if (sscanf(dstmac, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                    &intf->dst_mac[0],
                    &intf->dst_mac[1],
                    &intf->dst_mac[2],
                    &intf->dst_mac[3],
                    &intf->dst_mac[4],
                    &intf->dst_mac[5]) != 6) {
            upipe_err(upipe, "invalid dst macaddr");
            goto error;
        }
    } else {
        if (IN_MULTICAST(ntohl(intf->dst_ip))) {
            uint32_t ip = ntohl(intf->dst_ip);
            intf->dst_mac[0] = 0x01;
            intf->dst_mac[1] = 0x00;
            intf->dst_mac[2] = 0x5e;
            intf->dst_mac[3] = (ip >> 16) & 0x7f;
            intf->dst_mac[4] = (ip >>  8) & 0xff;
            intf->dst_mac[5] = (ip      ) & 0xff;
        } else {
            upipe_err(upipe, "unicast and dst mac address unspecified, use ?dstmac=YY:ZZ");
            goto error;
        }
    }

    free(ip);
    free(dstmac);

    return UBASE_ERR_NONE;

error:
    free(ip);
    free(dstmac);

    return UBASE_ERR_INVALID;
}

static int upipe_netmap_sink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);

    upipe_netmap_sink_set_upump(upipe, NULL);
    upipe_netmap_sink_check_upump_mgr(upipe);
    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_netmap_sink->uri = strdup(uri);
    if (unlikely(upipe_netmap_sink->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    char *p = strchr(upipe_netmap_sink->uri, '+');
    if (p)
        *p++ = '\0';

    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[0];
    UBASE_RETURN(upipe_netmap_sink_ip_params(upipe, intf, upipe_netmap_sink->uri));

    if (p) {
        p[-1] = '+';
        UBASE_RETURN(upipe_netmap_sink_ip_params(upipe, intf+1, p));
    }

    return UBASE_ERR_NONE;
}

/** @internal @This requires a ubuf manager by proxy, and amends the flow
 * format.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_netmap_sink_amend_ubuf_mgr(struct upipe *upipe,
                                        struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_format, &align)) || !align) {
        uref_pic_flow_set_align(flow_format, 32);
        align = 32;
    }


    if (align % 32) {
        align = align * 32 / ubase_gcd(align, 32);
        uref_pic_flow_set_align(flow_format, align);
    }

    return upipe_throw_provide_request(upipe, request);
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_netmap_sink_provide_flow_format(struct upipe *upipe,
                                              struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);

    uref_pic_flow_clear_format(flow_format);

    uref_pic_flow_set_macropixel(flow_format, 1);

    uint8_t plane;
    if (ubase_check(uref_pic_flow_find_chroma(request->uref, "y10l", &plane))) {
        uref_pic_flow_add_plane(flow_format, 1, 1, 2, "y10l");
        uref_pic_flow_add_plane(flow_format, 2, 1, 2, "u10l");
        uref_pic_flow_add_plane(flow_format, 2, 1, 2, "v10l");
    } else if (ubase_check(uref_pic_flow_find_chroma(request->uref, "y8", &plane))) {
        uref_pic_flow_add_plane(flow_format, 1, 1, 1, "y8");
        uref_pic_flow_add_plane(flow_format, 2, 1, 1, "u8");
        uref_pic_flow_add_plane(flow_format, 2, 1, 1, "v8");
    } else {
        uref_pic_flow_set_macropixel(flow_format, 6);
        uref_pic_flow_add_plane(flow_format, 1, 1, 16, "u10y10v10y10u10y10v10y10u10y10v10y10");
    }

    return urequest_provide_flow_format(request, flow_format);
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
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);

    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_netmap_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_netmap_sink_set_upump(upipe, NULL);
            return upipe_netmap_sink_attach_upump_mgr(upipe);
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_SINK_LATENCY)
                return urequest_provide_sink_latency(request, NETMAP_SINK_LATENCY);
            if (request->type == UREQUEST_UBUF_MGR)
                return upipe_netmap_sink_amend_ubuf_mgr(upipe, request);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_netmap_sink_provide_flow_format(upipe, request);
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
            upipe_netmap_sink_reset_counters(upipe);
            for (;;) {
                struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
                if (!uchain)
                    break;
                struct uref *uref = uref_from_uchain(uchain);
                uref_free(uref);
            }
            upipe_netmap_sink->uref = NULL;
            upipe_netmap_sink_set_upump(upipe, NULL);
            return upipe_netmap_sink_set_uri(upipe, uri);
        }
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

    uref_free(upipe_netmap_sink->flow_def);
    free(upipe_netmap_sink->uri);

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        free(intf->maxrate_uri);
        nm_close(intf->d);
        close(intf->fd);
    }

    upipe_netmap_sink_clean_upump(upipe);
    upipe_netmap_sink_clean_upump_mgr(upipe);
    upipe_netmap_sink_clean_urefcount(upipe);
    upipe_netmap_sink_clean_uclock(upipe);
    upipe_clean(upipe);
    free(upipe_netmap_sink);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_netmap_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NETMAP_SINK_SIGNATURE,

    .upipe_alloc = _upipe_netmap_sink_alloc,
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

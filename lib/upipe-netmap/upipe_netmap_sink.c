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
#include <upipe/uref_sound.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-netmap/upipe_netmap_sink.h>
#include <upipe/uprobe_prefix.h>

#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <pthread.h>
#include <limits.h>
#include <libgen.h>

#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>

#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>
#include <bitstream/ietf/rfc4175.h>
#include <bitstream/ietf/rfc8331.h>
#include <bitstream/smpte/2022_6_hbrmt.h>
#include <bitstream/ietf/rtp.h>
#include <bitstream/ieee/ethernet.h>

#include "sdi.h"

#include "../upipe-hbrmt/sdienc.h"
#include "../upipe-hbrmt/rfc4175_enc.h"
#include "../upipe-modules/upipe_udp.h"
#include "x86/avx512.h"
#include "utils.h"

#include <math.h>

#define UPIPE_RFC4175_MAX_PLANES 3
#define UPIPE_RFC4175_PIXEL_PAIR_BYTES 5

/* the maximum ever delay between 2 TX buffers refill */
#define NETMAP_SINK_LATENCY (UCLOCK_FREQ / 25)

#define UINT64_MSB(value)      ((value) & UINT64_C(0x8000000000000000))
#define UINT64_LOW_MASK(value) ((value) & UINT64_C(0x7fffffffffffffff))

#define AES67_MAX_PATHS 2
#define AES67_MAX_FLOWS 8
#define AES67_MAX_SAMPLES_PER_PACKET 48

#ifndef MTU
#define MTU 1500
#endif

#define MAX_AUDIO_UREFS 20

/* From pciutils' pci.ids */
#define VENDOR_ID_MELLANOX 0x15b3
#define DEVICE_ID_CONNECTX6DX 0x101d

static struct upipe_mgr upipe_netmap_sink_audio_mgr;

/** @hidden */
static bool upipe_netmap_sink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);

struct upipe_netmap_intf {
    /** Source */
    uint16_t src_port;
    in_addr_t src_ip;
    uint8_t src_mac[6];

    /* TODO: Better name for the struct. */
    struct destination source;

    /** Destination */
    uint16_t dst_port;
    in_addr_t dst_ip;
    uint8_t dst_mac[6];

    struct destination ancillary_dest;

    int vlan_id;

    /** Ring */
    unsigned int ring_idx;

    /** tx_maxrate sysctl uri */
    char *maxrate_uri;

    /** netmap descriptor **/
    struct nm_desc *d;

    int fd;
    struct ifreq ifr;

    /** packet headers */
    uint8_t header[HEADER_ETH_IP_UDP_LEN];
    uint8_t ancillary_header[HEADER_ETH_IP_UDP_LEN];
    uint8_t fake_header[HEADER_ETH_IP_UDP_LEN];
    int header_len; /* Same for all types of header */

    /** if interface is up */
    bool up;

    /** time at which intf came back up */
    uint64_t wait;

    char *device_base_name;
    unsigned vendor_id;
    unsigned device_id;
};

struct audio_packet_state {
    uint32_t num, den;
    uint16_t audio_counter;
    uint16_t video_counter, video_limit;
};

struct aes67_flow {
    struct destination dest;
    /* Raw Ethernet, optional vlan, IP, and UDP headers. */
    uint8_t header[HEADER_ETH_IP_UDP_LEN];
    /* Flow has been populated and packets should be sent. */
    bool populated;
};

struct upipe_netmap_sink_audio {
    /** public upipe structure */
    struct upipe upipe;
    /** buffered urefs */
    struct uchain urefs;
    uint64_t n;

    /** delay applied to systime attribute of urefs */
    uint64_t latency;
    /* Current uref. */
    struct uref *uref;
    /* Mapped data. */
    const int32_t *data;
    /* Size of mapped uref (samples). */
    size_t uref_samples;
    /* Number of channels in the uref. */
    uint8_t channels;

    /* Cached audio data (packed) from tails of input urefs. */
    uint8_t audio_data[AES67_MAX_SAMPLES_PER_PACKET * 16 * 3 + 1];
    /* Number of samples in buffer. */
    int cached_samples;

    /** maximum samples to put in each packet */
    int output_samples;
    /* Number of channels in each flow. */
    int output_channels;
    /* Maximum transmission unit. */
    int mtu;
    /* Configured packet size (no CRC). */
    uint16_t payload_size;

    /* Details for all destinations. */
    struct aes67_flow flows[AES67_MAX_FLOWS][AES67_MAX_PATHS];
    int num_flows;

    bool need_reconfig;
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
    uint64_t frame_duration;
    unsigned packet_size;
    /* Time of 1 packet in nanoseconds. */
    int32_t packet_duration;
    bool progressive;

    /* Determined by the input flow_def */
    bool rfc4175;
    int input_bit_depth;
    bool input_is_v210;

    /* RTP Header */
    uint8_t rtp_header[RTP_HEADER_SIZE + RFC_4175_EXT_SEQ_NUM_LEN + RFC_4175_HEADER_LEN];
    uint8_t audio_rtp_header[RTP_HEADER_SIZE];
    uint8_t ancillary_rtp_header[RTP_HEADER_SIZE];
    uint8_t rtp_pt_video;
    uint8_t rtp_pt_audio;
    uint8_t rtp_pt_ancillary;

    unsigned gap_fakes_current;
    unsigned gap_fakes;
    bool write_ancillary;
    uint64_t phase_delay;

    /* Cached timestamps for RFC4175 */
    uint64_t rtp_timestamp[2];

    /** picture size */
    uint64_t hsize;
    uint64_t vsize;

    /** sequence number **/
    uint64_t seqnum;
    /** ancillary sequence number **/
    uint64_t ancillary_seqnum;
    /* Number of frames since 1970. */
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

    double tx_rate_factor;
    uint64_t fakes;
    uint32_t step;
    int64_t needed_fakes;
    uint32_t pkts_in_frame;
    float pid_last_error;
    float pid_error_sum;
    float pid_last_output;
    /* Timestamp in UCLOCK_FREQ since 1970.  Needs 55 or more bits in 2020. */
    uint64_t frame_ts;
    uint64_t frame_ts_start;
    uint32_t prev_marker_seq;
    struct audio_packet_state audio_packet_state;

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

    struct upipe_netmap_sink_audio audio_subpipe;

    /** public upipe structure */
    struct upipe upipe;
};

struct ring_state {
    /* netmap ring struct */
    struct netmap_ring *txring;
    /* pointer to slot */
    struct netmap_slot *slot;
    /* pointer for slot data (headers and payload) */
    uint8_t *dst;
    /* skew relative to first path */
    int32_t skew;
    /* slot number to use (or be used) XXX: redundant?  in struct netmap_ring */
    uint32_t cur;
};

UPIPE_HELPER_UPIPE(upipe_netmap_sink, upipe, UPIPE_NETMAP_SINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_netmap_sink, urefcount, upipe_netmap_sink_free)
UPIPE_HELPER_UCLOCK(upipe_netmap_sink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)
UPIPE_HELPER_UPUMP_MGR(upipe_netmap_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_netmap_sink, upump, upump_mgr)

UPIPE_HELPER_UPIPE(upipe_netmap_sink_audio, upipe, UPIPE_NETMAP_SINK_AUDIO_SIGNATURE)
UBASE_FROM_TO(upipe_netmap_sink, upipe_netmap_sink_audio, audio_subpipe, audio_subpipe)

static int get_audio(struct upipe_netmap_sink_audio *audio_subpipe);
static void pack_audio(struct upipe_netmap_sink_audio *audio_subpipe);
static void handle_audio_tail(struct upipe_netmap_sink_audio *audio_subpipe);
static inline uint16_t audio_payload_size(uint16_t channels, uint16_t samples);
static inline void audio_copy_samples_to_packet(uint8_t *dst, const uint8_t *src,
        int output_channels, int output_samples, int channel_offset);

/* get MAC and/or IP address of specified interface */
static bool source_addr(const char *intf, struct destination *source)
{
    struct ifaddrs *ifaphead;
    if (getifaddrs(&ifaphead) != 0)
        return false;

    bool got_mac = false;
    bool got_ip = false;

    for (struct ifaddrs *ifap = ifaphead; ifap; ifap = ifap->ifa_next) {
        if (!ifap->ifa_addr)
            continue;

        if (strncmp(ifap->ifa_name, intf, IFNAMSIZ) != 0)
            continue;

        switch (ifap->ifa_addr->sa_family) {
        case AF_PACKET: /* interface mac address */
            source->sll = *(struct sockaddr_ll *)ifap->ifa_addr;
            got_mac = true;
            break;
        case AF_INET:
            source->sin = *(struct sockaddr_in *)ifap->ifa_addr;
            got_ip = true;
            break;
        }
    }

    freeifaddrs(ifaphead);
    return got_mac && got_ip;
}

/* Discover the actual HW device */
static int probe_hw(struct upipe_netmap_intf *intf, const char *ifname)
{
    char *path = NULL, *real_path = NULL, *base_name = NULL;
    int ret = 0;

    /* Format the device path. */
    ret = asprintf(&path, "/sys/class/net/%s/device", ifname);
    if (ret == -1) {
        ret = UBASE_ERR_ALLOC;
        goto error_hw_probe;
    }

    /* Resolve to the real path. */
    real_path = realpath(path, NULL);
    if (!real_path) {
        ret = UBASE_ERR_EXTERNAL;
        goto error_hw_probe;
    }
    free(path);
    path = NULL;

    /* Get the last directory component which represents the bus details. */
    char *temp = basename(real_path);
    if (!temp) {
        ret = UBASE_ERR_EXTERNAL;
        goto error_hw_probe;
    }
    /* "Both dirname() and basename() return pointers to null‐terminated strings.  (Do not pass these pointers to free(3).)" */
    base_name = strdup(temp);
    temp = NULL;
    if (!base_name) {
        ret = UBASE_ERR_ALLOC;
        goto error_hw_probe;
    }

    /* Get vendor ID. */
    char c = 0;
    FILE *fh = NULL;
    unsigned vendor = 0, device = 0;

    /* Format the file path. */
    ret = asprintf(&path, "%s/vendor", real_path);
    if (ret == -1) {
        ret = UBASE_ERR_ALLOC;
        goto error_hw_probe;
    }
    /* Open the file. */
    fh = fopen(path, "r");
    if (!fh) {
        //perror(path);
        ret = UBASE_ERR_EXTERNAL;
        goto error_hw_probe;
    }
    /* Scan the file. */
    ret = fscanf(fh, "%x%c", &vendor, &c);
    fclose(fh);
    /* If the scan did not read 2 values or the trailing character is not a
     * newline then it is an error. */
    if (ret != 2 || c != '\n') {
        //fprintf(stderr, "invalid scan\n");
        ret = UBASE_ERR_EXTERNAL;
        goto error_hw_probe;
    }
    free(path);
    path = NULL;

    /* Repeat for device ID. */
    ret = asprintf(&path, "%s/device", real_path);
    if (ret == -1) {
        ret = UBASE_ERR_ALLOC;
        goto error_hw_probe;
    }
    fh = fopen(path, "r");
    if (!fh) {
        ret = UBASE_ERR_EXTERNAL;
        goto error_hw_probe;
    }
    ret = fscanf(fh, "%x%c", &device, &c);
    fclose(fh);
    if (ret != 2 || c != '\n') {
        ret = UBASE_ERR_EXTERNAL;
        goto error_hw_probe;
    }

    intf->device_base_name = base_name;
    intf->vendor_id = vendor;
    intf->device_id = device;

    free(path);
    free(real_path);
    return UBASE_ERR_NONE;

error_hw_probe:
    free(path);
    free(real_path);
    free(base_name);

    return ret;
}

static int upipe_netmap_sink_open_intf(struct upipe *upipe,
        struct upipe_netmap_intf *intf, const char *uri)
{
    const size_t netmap_prefix_len = strlen("netmap:");
    int ret = UBASE_ERR_NONE;

    if (sscanf(uri, "netmap:%*[^-]-%u/T", &intf->ring_idx) != 1) {
        intf->ring_idx = 0;
    }

    char *netmap_device = strdup(uri);
    char *intf_addr = strdup(uri + netmap_prefix_len);
    if (!netmap_device || !intf_addr) {
        ret = UBASE_ERR_ALLOC;
        goto error;
    }

    /* Terminate at the '-' because that is not part of the interface name. */
    char *netmap_suffix = strchr(intf_addr, '-');
    *netmap_suffix = '\0';

    /* Get the IP and MAC addressed for the (vlan) interface. */
    if (!source_addr(intf_addr, &intf->source)) {
        upipe_err_va(upipe, "Could not read interface address for '%s'", intf_addr);
        ret = UBASE_ERR_INVALID;
        goto error;
    }
    intf->src_ip = intf->source.sin.sin_addr.s_addr;
    memcpy(intf->src_mac, intf->source.sll.sll_addr, ETHERNET_ADDR_LEN);

    /* Find the first '.' for the base interface name. */
    char *dot = strchr(netmap_device, '.');
    if (dot) {
        /* Read the vlan id from the next character. */
        int vlan_id = atoi(dot+1);
        if (vlan_id <= 0 || vlan_id >= 1<<12) {
            upipe_err_va(upipe, "invalid vlan id: %d", vlan_id);
            ret = UBASE_ERR_INVALID;
            goto error;
        }
        intf->vlan_id = vlan_id;

        /* Copy the netmap device suffix over the vlan id. */
        *dot = '-';
        strcpy(dot+1, netmap_suffix+1);

        /* Truncate at the '.' for the base interface. */
        *strchr(intf_addr, '.') = '\0';
    }

    /* Copy (base) interface name into the struct ifreq. */
    strncpy(intf->ifr.ifr_name, intf_addr, IFNAMSIZ);

    intf->d = nm_open(netmap_device, NULL, 0, 0);
    if (unlikely(!intf->d)) {
        upipe_err_va(upipe, "can't open netmap socket %s", netmap_device);
        ret = UBASE_ERR_EXTERNAL;
        goto error;
    }
    if (intf->d->req.nr_tx_slots < 4096) {
        upipe_err_va(upipe, "Card is not giving enough slots (%u)",
                intf->d->req.nr_tx_slots);
        nm_close(intf->d);
        ret = UBASE_ERR_EXTERNAL;
        goto error;
    }

    if (asprintf(&intf->maxrate_uri,
                "/sys/class/net/%s/queues/tx-%d/tx_maxrate",
                intf_addr, intf->ring_idx) < 0) {
        nm_close(intf->d);
        ret = UBASE_ERR_ALLOC;
        goto error;
    }

    if (ubase_check(probe_hw(intf, intf_addr))) {
        upipe_dbg_va(upipe, "%s: base name: %s, vendor: %u (%#x), device: %u (%#x)",
                intf_addr, intf->device_base_name,
                intf->vendor_id, intf->vendor_id,
                intf->device_id, intf->device_id);
    } else {
        upipe_warn_va(upipe, "error probing HW for '%s'", intf_addr);
    }

error:
    free(netmap_device);
    free(intf_addr);

    return ret;
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

    if (upipe_netmap_sink->intf[0].vendor_id == VENDOR_ID_MELLANOX
            && upipe_netmap_sink->intf[0].device_id == DEVICE_ID_CONNECTX6DX
            && upipe_netmap_sink->intf[1].vendor_id == VENDOR_ID_MELLANOX
            && upipe_netmap_sink->intf[1].device_id == DEVICE_ID_CONNECTX6DX)
    {
        /* Device is a Mellanox ConnectX-6 Dx assuming REAL_TIME_CLOCK_ENABLE=1 */
        /* TODO: eventually add detection of the config variables. */
        upipe_netmap_sink->tx_rate_factor = 1.0001;
    }

    free((char*)dev);

    upipe_notice_va(upipe, "opened %d netmap device(s)", p ? 2 : 1);

    return UBASE_ERR_NONE;
}

static void upipe_netmap_sink_reset_counters(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    /* Reset progression of current uref. */
    upipe_netmap_sink->line = 0;

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
    upipe_netmap_sink->ancillary_seqnum = 0;
    upipe_netmap_sink->frame_count = 0;
    upipe_netmap_sink->phase_delay = 0;
    memset(upipe_netmap_sink->rtp_timestamp, 0, sizeof(upipe_netmap_sink->rtp_timestamp));
    upipe_netmap_sink->frame_ts = 0;
    upipe_netmap_sink->frame_ts_start = 0;
}

static void upipe_netmap_sink_clear_queues(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    /* Clear buffered video urefs */
    for (;;) {
        struct uchain *uchain = ulist_pop(&upipe_netmap_sink->sink_queue);
        if (!uchain)
            break;
        struct uref *uref = uref_from_uchain(uchain);
        uref_free(uref);
    }
    upipe_netmap_sink->n = 0;

    /* Clear buffered audio urefs */
    for (;;) {
        struct uchain *uchain = ulist_pop(&upipe_netmap_sink->audio_subpipe.urefs);
        if (!uchain)
            break;
        struct uref *uref = uref_from_uchain(uchain);
        uref_free(uref);
    }
    upipe_netmap_sink->audio_subpipe.n = 0;
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
    upipe_netmap_sink->write_ancillary = false;
    upipe_netmap_sink->tx_rate_factor = 1;

    upipe_netmap_sink->uri = NULL;
    for (size_t i = 0; i < 2; i++) {
        memset(&upipe_netmap_sink->intf[i], 0, sizeof upipe_netmap_sink->intf[i]);
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        intf->vlan_id = -1;
        intf->up = true;
        intf->fd = socket(AF_INET, SOCK_DGRAM, 0);
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
        upipe_netmap_sink->unpack_v210 = upipe_v210_to_sdi_ssse3;
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
        upipe_netmap_sink->pack2_10_planar = upipe_planar_to_sdi_10_2_avx2;
        upipe_netmap_sink->pack2_8_planar = upipe_planar_to_sdi_8_2_avx2;
        upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_avx2;
        upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_avx2;
        upipe_netmap_sink->unpack_v210 = upipe_v210_to_sdi_avx2;
    }

    if (has_avx512_support()) {
        upipe_netmap_sink->pack2_8_planar = upipe_planar_to_sdi_8_2_avx512;
        upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_avx512;
    }

    if (has_avx512icl_support()) {
        upipe_netmap_sink->pack = upipe_uyvy_to_sdi_avx512icl;
        upipe_netmap_sink->pack2 = upipe_uyvy_to_sdi_2_avx512icl;
        upipe_netmap_sink->pack2_10_planar = upipe_planar_to_sdi_10_2_avx512icl;
        upipe_netmap_sink->pack2_8_planar = upipe_planar_to_sdi_8_2_avx512icl;
        upipe_netmap_sink->pack_10_planar = upipe_planar_to_sdi_10_avx512icl;
        upipe_netmap_sink->pack_8_planar = upipe_planar_to_sdi_8_avx512icl;
    }
#endif
#endif

    memset(upipe_netmap_sink->rtp_header, 0, sizeof upipe_netmap_sink->rtp_header);
    memset(upipe_netmap_sink->audio_rtp_header, 0, sizeof upipe_netmap_sink->audio_rtp_header);
    memset(upipe_netmap_sink->ancillary_rtp_header, 0, sizeof upipe_netmap_sink->ancillary_rtp_header);
    upipe_netmap_sink->rtp_pt_video = 96;
    upipe_netmap_sink->rtp_pt_audio = 97;
    upipe_netmap_sink->rtp_pt_ancillary = 98;

    /*
     * Audio subpipe.
     */
    memset(&upipe_netmap_sink->audio_subpipe, 0, sizeof upipe_netmap_sink->audio_subpipe);
    struct upipe_netmap_sink_audio *audio_subpipe = upipe_netmap_sink_to_audio_subpipe(upipe_netmap_sink);
    struct upipe *subpipe = upipe_netmap_sink_audio_to_upipe(audio_subpipe);

    upipe_init(upipe_netmap_sink_audio_to_upipe(audio_subpipe),
                &upipe_netmap_sink_audio_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_VERBOSE, "audio"));
    ulist_init(&audio_subpipe->urefs);
    subpipe->refcount = &upipe_netmap_sink->urefcount;

    audio_subpipe->output_samples = 6; /* TODO: other default to catch user not setting this? */
    audio_subpipe->output_channels = 16;
    audio_subpipe->mtu = MTU;
    audio_subpipe->payload_size = audio_payload_size(audio_subpipe->output_channels, audio_subpipe->output_samples);
    audio_subpipe->need_reconfig = true;

    upipe_throw_ready(upipe);
    upipe_throw_ready(subpipe);
    return upipe;
}

/* Cache RFC 4175 timestamps to avoid calculating them per packet */
static void upipe_netmap_update_timestamp_cache(struct upipe_netmap_sink *upipe_netmap_sink)
{
    __uint128_t t = upipe_netmap_sink->frame_count, t2 = upipe_netmap_sink->frame_count;
    const struct urational *fps = &upipe_netmap_sink->fps;

    t *= 90000;
    t *= fps->den;
    t /= fps->num;
    upipe_netmap_sink->rtp_timestamp[0] = t;

    if (!upipe_netmap_sink->progressive) {
        t2 *= 90000;
        t2 += 90000 / 2;
        t2 *= fps->den;
        t2 /= fps->num;
        upipe_netmap_sink->rtp_timestamp[1] = t2;
    }
}

static int upipe_netmap_put_rtp_headers(struct upipe_netmap_sink *upipe_netmap_sink, uint8_t *buf,
        uint8_t marker, uint8_t pt, uint64_t seqnum, bool update, bool f2)
{
    uint64_t *buf64 = (uint64_t*)buf;
    uint32_t *ssrc = (uint32_t*)(buf+8);

#if 0
    memset(buf, 0, RTP_HEADER_SIZE);
    rtp_set_hdr(buf);
    if (marker)
        rtp_set_marker(buf);
    rtp_set_type(buf, pt);
#endif

#define bswap64 __builtin_bswap64

    if (update) {
        uint64_t timestamp;
        if (upipe_netmap_sink->rfc4175) {
            timestamp = upipe_netmap_sink->rtp_timestamp[f2];
        } else {
            timestamp = upipe_netmap_sink->rtp_timestamp[0];
        }

#if 0
        rtp_set_seqnum(buf, seqnum & UINT16_MAX);
        rtp_set_timestamp(buf, timestamp & UINT32_MAX);
#endif
        *buf64 = bswap64((UINT64_C(0x80) << 56) | ((uint64_t)marker << 55) | ((uint64_t)pt << 48) |
                         ((uint64_t)(seqnum & UINT16_MAX) << 32) | (uint64_t)(timestamp & UINT32_MAX));
    }
    else {
        *buf64 = bswap64((UINT64_C(0x80) << 56) | ((uint64_t)marker << 55) | ((uint64_t)pt << 48));
    }

#undef bswap64

    *ssrc = 0;

    return RTP_HEADER_SIZE;
}

static int upipe_netmap_put_ip_headers(struct upipe_netmap_intf *intf,
        uint8_t *buf, uint16_t payload_size)
{
    int ret = ETHERNET_HEADER_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE;
    /* Destination MAC */
    ethernet_set_dstaddr(buf, intf->dst_mac);

    /* Source MAC */
    ethernet_set_srcaddr(buf, intf->src_mac);

    /* Ethertype */
    if (intf->vlan_id < 0) {
        ethernet_set_lentype(buf, ETHERNET_TYPE_IP);
    }

    /* VLANs */
    else {
        ethernet_set_lentype(buf, ETHERNET_TYPE_VLAN);
        ethernet_vlan_set_priority(buf, 0);
        ethernet_vlan_set_cfi(buf, 0);
        ethernet_vlan_set_id(buf, intf->vlan_id);
        ethernet_vlan_set_lentype(buf, ETHERNET_TYPE_IP);
        ret += ETHERNET_VLAN_LEN;
    }

    buf = ethernet_payload(buf);

    /* 0x1c - Standard, low delay, high throughput, high reliability TOS */
    upipe_udp_raw_fill_headers(buf, intf->src_ip,
                               intf->dst_ip,
                               intf->src_port,
                               intf->dst_port,
                               10, 0x1c, payload_size);

    /* Make header for fake packets. */
    buf = intf->fake_header;
    ethernet_set_dstaddr(buf, intf->src_mac);
    ethernet_set_srcaddr(buf, intf->src_mac);
    if (intf->vlan_id < 0) {
        ethernet_set_lentype(buf, ETHERNET_TYPE_IP);
    } else {
        ethernet_set_lentype(buf, ETHERNET_TYPE_VLAN);
        ethernet_vlan_set_priority(buf, 0);
        ethernet_vlan_set_cfi(buf, 0);
        ethernet_vlan_set_id(buf, intf->vlan_id);
        ethernet_vlan_set_lentype(buf, ETHERNET_TYPE_IP);
    }
    buf = ethernet_payload(buf);
    upipe_udp_raw_fill_headers(buf, intf->src_ip,
                               intf->src_ip,
                               intf->src_port,
                               intf->dst_port,
                               10, 0x1c, payload_size);

    return ret;
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
                                     const uint16_t len, const bool field_id, uint16_t line_number,
                                     uint8_t continuation, uint16_t offset)
{
    if (field_id)
        line_number -= upipe_netmap_sink->vsize / 2;

#if 0
    rfc4175_set_extended_sequence_number(buf, (upipe_netmap_sink->seqnum >> 16) & UINT16_MAX);
    buf += RFC_4175_EXT_SEQ_NUM_LEN;
    memset(buf, 0, RFC_4175_HEADER_LEN);
    rfc4175_set_line_length(buf, len);
    rfc4175_set_line_field_id(buf, field_id);
    rfc4175_set_line_number(buf, line_number);
    rfc4175_set_line_continuation(buf, continuation);
    rfc4175_set_line_offset(buf, offset);
#endif

    uint64_t *buf64 = (uint64_t*)buf;

#define bswap64 __builtin_bswap64

    *buf64 = bswap64((((upipe_netmap_sink->seqnum >> 16) & UINT16_MAX) << 48) | ((uint64_t)len << 32) |
                     ((uint64_t)field_id << 31) | ((uint64_t)line_number << 16) | (uint64_t)offset);

#undef bswap64

    return RFC_4175_EXT_SEQ_NUM_LEN + RFC_4175_HEADER_LEN;
}

static inline int get_interleaved_line(struct upipe_netmap_sink *upipe_netmap_sink)
{
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

static inline void setup_gap_fakes(struct upipe_netmap_sink *upipe_netmap_sink, bool progressive)
{
    upipe_netmap_sink->gap_fakes_current = upipe_netmap_sink->gap_fakes;
    if (!progressive)
        upipe_netmap_sink->gap_fakes_current /= 2;
    upipe_netmap_sink->write_ancillary = true;
}

/* returns 1 if uref exhausted */
static int worker_rfc4175(struct upipe_netmap_sink *upipe_netmap_sink, uint8_t **dst, uint16_t **len, uint64_t **ptr)
{
    const bool progressive = upipe_netmap_sink->progressive;
    const bool copy = dst[1] != NULL && dst[0] != NULL;
    const int idx = (dst[0] != NULL) ? 0 : 1;
    bool eof = false; /* End of frame */

    const uint16_t header_size = upipe_netmap_sink->intf[0].header_len;
    const uint16_t rtp_rfc_header_size = RTP_HEADER_SIZE + RFC_4175_EXT_SEQ_NUM_LEN + RFC_4175_HEADER_LEN;
    const uint16_t eth_frame_len = header_size + rtp_rfc_header_size + upipe_netmap_sink->payload;
    const uint16_t pixels1 = upipe_netmap_sink->payload * 2 / UPIPE_RFC4175_PIXEL_PAIR_BYTES;

    bool marker = 0, continuation = 0;

    const bool field = progressive ? 0 : upipe_netmap_sink->line >= upipe_netmap_sink->vsize / 2;

    /* End of the line */
    if (upipe_netmap_sink->pixel_offset + pixels1 >= upipe_netmap_sink->hsize) {
        /* End of the field or frame */
        if ((!progressive && upipe_netmap_sink->line+1 == (upipe_netmap_sink->vsize/2)) ||
                upipe_netmap_sink->line+1 == upipe_netmap_sink->vsize) {

            marker = 1;

            if (upipe_netmap_sink->line+1 == upipe_netmap_sink->vsize)
                eof = true;
        }

        //pixels1 = upipe_netmap_sink->hsize - upipe_netmap_sink->pixel_offset;
    }

    const uint16_t data_len1 = upipe_netmap_sink->payload;

    upipe_netmap_put_rtp_headers(upipe_netmap_sink, upipe_netmap_sink->rtp_header,
            marker, upipe_netmap_sink->rtp_pt_video, upipe_netmap_sink->seqnum, true, field);
    upipe_put_rfc4175_headers(upipe_netmap_sink, upipe_netmap_sink->rtp_header + RTP_HEADER_SIZE, data_len1,
                              field, upipe_netmap_sink->line, continuation, upipe_netmap_sink->pixel_offset);

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (unlikely(!intf->d || !intf->up))
            continue;

        /* Ethernet/IP Header */
        memcpy(dst[i], intf->header, header_size);
        dst[i] += header_size;

        /* RTP HEADER */
        memcpy(dst[i], upipe_netmap_sink->rtp_header, rtp_rfc_header_size);
        dst[i] += rtp_rfc_header_size;
    }

    upipe_netmap_sink->pkt++;
    upipe_netmap_sink->seqnum++;
    upipe_netmap_sink->seqnum &= UINT32_MAX;

    int interleaved_line = get_interleaved_line(upipe_netmap_sink);
    if (upipe_netmap_sink->input_is_v210) {
        const uint8_t *src = upipe_netmap_sink->pixel_buffers[0] +
            upipe_netmap_sink->strides[0]*interleaved_line;

        int block_offset = upipe_netmap_sink->pixel_offset / upipe_netmap_sink->output_pixels_per_block;
        src += block_offset * upipe_netmap_sink->output_block_size;

        upipe_netmap_sink->unpack_v210((uint32_t*)src, dst[idx], pixels1);
        /* FIXME: support 2022-7 with v210 */
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

        if (likely(copy))
            upipe_netmap_sink->pack2_8_planar(y8, u8, v8, dst[0], dst[1], pixels1);
        else if (dst[idx])
            upipe_netmap_sink->pack_8_planar(y8, u8, v8, dst[idx], pixels1);
    }
    else if (upipe_netmap_sink->input_bit_depth == 10) {
        const uint16_t *y10, *u10, *v10;
        y10 = (uint16_t*)(upipe_netmap_sink->pixel_buffers[0] +
             upipe_netmap_sink->strides[0] * interleaved_line) +
             upipe_netmap_sink->pixel_offset / 1;
        u10 = (uint16_t*)(upipe_netmap_sink->pixel_buffers[1] +
             upipe_netmap_sink->strides[1] * interleaved_line) +
             upipe_netmap_sink->pixel_offset / 2;
        v10 = (uint16_t*)(upipe_netmap_sink->pixel_buffers[2] +
             upipe_netmap_sink->strides[2] * interleaved_line) +
             upipe_netmap_sink->pixel_offset / 2;

        if (likely(copy))
            upipe_netmap_sink->pack2_10_planar(y10, u10, v10, dst[0], dst[1], pixels1);
        else if (dst[idx])
            upipe_netmap_sink->pack_10_planar(y10, u10, v10, dst[idx], pixels1);
    }

    upipe_netmap_sink->pixel_offset += pixels1;
    if (upipe_netmap_sink->pixel_offset >= upipe_netmap_sink->hsize)
        continuation = 1;

    *dst += data_len1;

    /* End of the line or end of the field or frame */
    if (continuation || marker) {
        upipe_netmap_sink->pixel_offset = 0;
        if (continuation || (marker && !field)) {
            upipe_netmap_sink->line++;
        }
    }

    upipe_netmap_sink->bits += (eth_frame_len + 4 /* CRC */) * 8;

    /* packet size and end of frame flag */
    if (likely(copy)) {
        *len[0] = *len[1] = eth_frame_len;
        *ptr[0] = *ptr[1] = (uint64_t)eof << 63;
    } else if (len[idx]) {
        *len[idx] = eth_frame_len;
        *ptr[idx] = (uint64_t)eof << 63;
    }

    /* Release consumed frame */
    if (unlikely(eof)) {
        upipe_netmap_sink->line = 0;
        upipe_netmap_sink->pixel_offset = 0;
        upipe_netmap_sink->frame_count++;
        upipe_netmap_sink->pkt = 0;
        upipe_netmap_update_timestamp_cache(upipe_netmap_sink);
        return 1;
    }

    return 0;
}

static int worker_hbrmt(struct upipe_netmap_sink *upipe_netmap_sink, uint8_t **dst, const uint8_t *src,
        int bytes_left, uint16_t **len, uint64_t **ptr)
{
    const uint8_t packed_bytes = upipe_netmap_sink->packed_bytes;
    const uint16_t header_size = upipe_netmap_sink->intf[0].header_len;
    const uint16_t rtp_hbrmt_header_size = RTP_HEADER_SIZE + HBRMT_HEADER_SIZE;
    const uint16_t eth_frame_len = header_size + rtp_hbrmt_header_size + HBRMT_DATA_SIZE;
    const bool copy = dst[1] != NULL && dst[0] != NULL;
    const int idx = (dst[0] != NULL) ? 0 : 1;

    /* available payload size */
    int pack_bytes_left = bytes_left * 5 / 8 + packed_bytes;

    /* desired payload size */
    int payload_len = HBRMT_DATA_SIZE;
    if (unlikely(payload_len > pack_bytes_left))
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

    /* update rtp header */
    uint8_t *rtp = upipe_netmap_sink->rtp_header;
    rtp_set_seqnum(rtp, upipe_netmap_sink->seqnum & UINT16_MAX);
    rtp_set_timestamp(rtp, timestamp & UINT32_MAX);
    if (unlikely(end))
        rtp_set_marker(rtp);

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (unlikely(!intf->d || !intf->up))
            continue;

        /* Ethernet/IP Header */
        memcpy(dst[i], intf->header, header_size);
        dst[i] += header_size;

        /* RTP HEADER */
        memcpy(dst[i], upipe_netmap_sink->rtp_header, rtp_hbrmt_header_size);
        dst[i] += rtp_hbrmt_header_size;

        /* use previous scratch buffer */
        if (likely(packed_bytes)) {
            memcpy(dst[i], upipe_netmap_sink->packed_pixels, packed_bytes);
            dst[i] += packed_bytes;
        }
    }

    /* unset rtp marker if needed */
    if (unlikely(end))
        rtp_clear_marker(rtp);

    upipe_netmap_sink->seqnum++;

    /* convert pixels */
    if (likely(copy))
        upipe_netmap_sink->pack2(dst[0], dst[1], src, pixels);
    else if (dst[idx])
        upipe_netmap_sink->pack(dst[idx], src, pixels);

    /* bytes these pixels decoded to */
    int bytes = pixels * 4 * 5 / 8;

    /* overlap */
    int pkt_rem = bytes - (payload_len - packed_bytes);
    assert(pkt_rem <= sizeof(upipe_netmap_sink->packed_pixels));
    if (likely(pkt_rem > 0 && dst[idx]))
        memcpy(upipe_netmap_sink->packed_pixels, dst[idx] + bytes - pkt_rem, pkt_rem);

    /* update overlap count */
    upipe_netmap_sink->packed_bytes = pkt_rem;

    /* padding */
    if (unlikely(payload_len != HBRMT_DATA_SIZE)) {
        if (dst[idx])
            memset(dst[idx] + payload_len, 0, HBRMT_DATA_SIZE - payload_len);
        if (copy)
            memset(dst[1] + payload_len, 0, HBRMT_DATA_SIZE - payload_len);
    }

    /* packet size and end of frame flag */
    if (likely(copy)) {
        *len[0] = *len[1] = eth_frame_len;
        *ptr[0] = *ptr[1] = (uint64_t)end << 63;
    } else if (len[idx]) {
        *len[idx] = eth_frame_len;
        *ptr[idx] = (uint64_t)end << 63;
    }

    if (unlikely(end)) {
        upipe_netmap_sink->packed_bytes = 0;
        return bytes_left;
    }

    return pixels * 4;
}

static void write_ancillary(struct upipe_netmap_sink *upipe_netmap_sink, uint8_t **dst, uint16_t **len, uint64_t **ptr)
{
    const bool progressive = upipe_netmap_sink->progressive;
    const bool copy = dst[1] != NULL && dst[0] != NULL;
    const int idx = (dst[0] != NULL) ? 0 : 1;
    const bool field = progressive ? 0 : upipe_netmap_sink->line >= upipe_netmap_sink->vsize / 2;
    const bool marker = 1;

    const uint16_t header_size = upipe_netmap_sink->intf[0].header_len;
    const uint16_t payload_size = RTP_HEADER_SIZE + RFC_8331_HEADER_LEN;
    const uint16_t eth_frame_len = header_size + payload_size;

    uint8_t rfc_8331_header[RFC_8331_HEADER_LEN];

    upipe_netmap_put_rtp_headers(upipe_netmap_sink, upipe_netmap_sink->ancillary_rtp_header,
        marker, upipe_netmap_sink->rtp_pt_ancillary, upipe_netmap_sink->ancillary_seqnum, true, field);
  
    /* RFC 8331 Headers */
    const uint8_t f = progressive ? RFC_8331_F_PROGRESSIVE : field ? RFC_8331_F_FIELD_2 : RFC_8331_F_FIELD_1;
    rfc8331_set_extended_sequence_number(rfc_8331_header, (uint16_t)((upipe_netmap_sink->ancillary_seqnum >> 16) & UINT16_MAX));
    rfc8331_set_length(rfc_8331_header, 0);
    rfc8331_set_anc_count(rfc_8331_header, 0);
    rfc8331_set_f(rfc_8331_header, f);

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (unlikely(!intf->d || !intf->up))
            continue;

        /* TODO: improve this by not doing the ethernet header everytime? */
        make_header(intf[i].ancillary_header, &intf[i].source, &intf[i].ancillary_dest,
                intf[i].vlan_id, payload_size);

        /* Ethernet/IP Header */
        memcpy(dst[i], intf->ancillary_header, header_size);
        dst[i] += header_size;

        /* RTP HEADER */
        memcpy(dst[i], upipe_netmap_sink->ancillary_rtp_header, RTP_HEADER_SIZE);
        dst[i] += RTP_HEADER_SIZE;

        /* RFC 8331 Header */
        memcpy(dst[i], rfc_8331_header, RFC_8331_HEADER_LEN);
        dst[i] += RFC_8331_HEADER_LEN;
    }

    upipe_netmap_sink->ancillary_seqnum++;
    upipe_netmap_sink->ancillary_seqnum &= UINT32_MAX;

    upipe_netmap_sink->bits += (eth_frame_len + 4 /* CRC */) * 8;

    /* write packet size */
    if (likely(copy)) {
        *len[0] = *len[1] = eth_frame_len;
        *ptr[0] = *ptr[1] = 0;
    } else if (len[idx]) {
        *len[idx] = eth_frame_len;
        *ptr[idx] = 0;
    }
}

static float pts_to_time(uint64_t pts)
{
    return (float)pts / 27000;
}

static void upipe_clear_queues(struct upipe *upipe, struct upipe_netmap_intf *intf, uint32_t packets, int increment)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    struct netmap_ring *txring = NETMAP_TXRING(intf->d->nifp, intf->ring_idx);

    const unsigned len = upipe_netmap_sink->packet_size;

    uint32_t cur = txring->cur;
    for (uint32_t i = 0; i < packets; i++) {
        uint8_t *dst = (uint8_t*)NETMAP_BUF(txring, txring->slot[cur].buf_idx);
        memset(dst, 0, len);
        memcpy(dst, intf->header, ETHERNET_HEADER_LEN);
        txring->slot[cur].len = len;
        txring->slot[cur].ptr = 0;
        cur = nm_ring_next(txring, cur);
    }

    if (increment)
        txring->head = txring->cur = cur;
}

static struct uref *get_uref(struct upipe *upipe)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    struct upipe_netmap_sink_audio *audio_subpipe = &upipe_netmap_sink->audio_subpipe;

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

            /* Drop associated sound uref */
            if (audio_subpipe->uref) {
                uref_sound_unmap(audio_subpipe->uref, 0, -1, 1);
                uref_free(audio_subpipe->uref);
                audio_subpipe->uref = NULL;
            }
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

                pts -= upipe_netmap_sink->phase_delay;

                /* Remove any nearby audio urefs */
                struct uchain *uchain, *uchain_tmp;
                ulist_delete_foreach(&audio_subpipe->urefs, uchain, uchain_tmp) {
                    struct uref *uref = uref_from_uchain(uchain);
                    uint64_t pts_audio = 0;
                    uref_clock_get_pts_sys(uref, &pts_audio);
                    pts_audio += audio_subpipe->latency;
                    if (pts - pts_audio < 27000 || pts_audio - pts < 27000) {
                        ulist_delete(uchain);
                        uref_free(uref_from_uchain(uchain));
                    }
                }
        }
    }

    upipe_netmap_sink->uref = uref;
    return uref;
}

static float compute_fakes(struct upipe *upipe, float j)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    const struct urational *fps = &upipe_netmap_sink->fps;

    if (j > 300)
        j = 300;
    if (j < -300)
        j = -300;
    float i = j;

    float error = -i;
    upipe_netmap_sink->pid_error_sum += error * fps->den / fps->num;

    /* avoid swinging too far when initial error is large */
    if (upipe_netmap_sink->pid_error_sum > 500)
        upipe_netmap_sink->pid_error_sum = 500;

    float d = (error - upipe_netmap_sink->pid_last_error) * fps->num / fps->den;
    if (fps->num / fps->den >= 50)
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
    return upipe_netmap_sink->pid_last_output;
}

static void handle_tx_stamp(struct upipe *upipe, uint64_t t, uint16_t seq)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    const int64_t dur = UCLOCK_FREQ * upipe_netmap_sink->fps.den / upipe_netmap_sink->fps.num;
    assert(t);

    t /= 1000;
    t *= 27;
    /* t is the TX time of the marker packet, now in UCLOCK_FREQ ticks. */

    /* HACK: start from the second timestamp, sometimes the hardware gives nonsense timestamps
       Why? Is this our fault for some reason?? */
    if (upipe_netmap_sink->frame_ts == 0) {
        upipe_netmap_sink->frame_ts = 1;
        return;
    }

    if (upipe_netmap_sink->frame_ts == 1) {
        upipe_netmap_sink->prev_marker_seq = seq;

        /* Calculate the frame timestamp based on the *next* PTP frame tick */
        upipe_netmap_sink->frame_ts_start = upipe_netmap_sink->frame_ts = t;

        /* floor(frame_ts) to the nearest PTP frame tick */
        upipe_netmap_sink->frame_ts /= dur;

        /* ceil(frame_ts) to the nearest PTP frame tick (i.e the next frame) */
        upipe_netmap_sink->frame_count = upipe_netmap_sink->frame_ts + 1;

        /* back to 27MHz units (having been floored) */
        upipe_netmap_sink->frame_ts *= dur;

        upipe_netmap_sink->phase_delay = dur - (t - upipe_netmap_sink->frame_ts);
        upipe_netmap_update_timestamp_cache(upipe_netmap_sink);

        return;
    }

    uint16_t s = seq - upipe_netmap_sink->prev_marker_seq;

    /* If the seqnum is the same this is probably the same marker packet being
     * read on the rings at different times due to the skew adjustment.  If s is
     * 0 then it will cause an underflow in the next block and a number of
     * frames appearing to be missed. */
    if (s == 0)
        return;

    if (s != upipe_netmap_sink->packets_per_frame) { /* we missed a marker */
        uint64_t frames = (s - 1) / upipe_netmap_sink->packets_per_frame;
        upipe_warn_va(upipe, "Missed %" PRIu64 " marker frames, prev_marker_seq: %u, seq: %u",
                         frames, upipe_netmap_sink->prev_marker_seq, seq);
        upipe_netmap_sink->frame_ts += frames * dur;
    }
    upipe_netmap_sink->prev_marker_seq = seq;

    upipe_netmap_sink->frame_ts += dur;

    int64_t x = t - upipe_netmap_sink->frame_ts;
    float ideal = (dur - x) * (float)upipe_netmap_sink->packets_per_frame / dur;

    float step = compute_fakes(upipe, -ideal);
    float needed_fakes = (upipe_netmap_sink->needed_fakes * 9.f + step) / 10.f;

    if (needed_fakes < 0.f) // if we're too late, just wait till we drift back
        needed_fakes = 0.f;

    upipe_netmap_sink->needed_fakes = needed_fakes + 0.5f; /* use a math library function */

    if (upipe_netmap_sink->needed_fakes) {
        uint64_t total = upipe_netmap_sink->packets_per_frame;
        if (upipe_netmap_sink->rfc4175)
            total += upipe_netmap_sink->gap_fakes;
        upipe_netmap_sink->step = (total + upipe_netmap_sink->needed_fakes - 1) / upipe_netmap_sink->needed_fakes;
    } else
        upipe_netmap_sink->step = 0;

    upipe_dbg_va(upipe,
            "% .3f ms, ideal % .2f step % .2f, fakes %u needed fakes %.2f",
            (double)(dur - x) / 27000., ideal, step,
            (unsigned)upipe_netmap_sink->fakes, needed_fakes);
    upipe_netmap_sink->fakes = 0;

    upipe_netmap_sink->pkts_in_frame = 0;
}

static inline void check_marker_packet(struct upipe_netmap_sink *upipe_netmap_sink,
        struct ring_state ring_state[2])
{
    struct upipe *upipe = upipe_netmap_sink_to_upipe(upipe_netmap_sink);

    int64_t tx_stamp[2] = { 0, 0 };
    bool stamped = false;
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (unlikely(!intf->d || !intf->up)) {
            /* clear the pointers if the interface is down. */
            ring_state[i].dst = NULL;
            ring_state[i].slot = NULL;
            continue;
        }

        struct netmap_ring *txring = ring_state[i].txring;
        uint32_t cur = ring_state[i].cur;
        struct netmap_slot *slot = ring_state[i].slot = &txring->slot[cur];

        uint8_t *dst = ring_state[i].dst = (uint8_t *)NETMAP_BUF(txring, txring->slot[cur].buf_idx);
        uint8_t *rtp = dst + intf->header_len;

        /* Check for marker bit indicating end of frame/field and get exact TX
         * time of that packet. */
        if (UINT64_MSB(slot->ptr)) {
            tx_stamp[i] = UINT64_LOW_MASK(slot->ptr);
            if (!stamped && tx_stamp[i]) {
                uint16_t seq = rtp_get_seqnum(rtp);
                handle_tx_stamp(upipe, tx_stamp[i], seq);
                stamped = true;
            }

            /* record skew */
            if (tx_stamp[0] > 0) {
                ring_state[i].skew = tx_stamp[i] - tx_stamp[0];
            }
        }
    }
}

static inline void advance_ring_state(struct ring_state *ring_state)
{
    struct netmap_ring *txring = ring_state->txring;
    uint32_t cur = ring_state->cur = nm_ring_next(txring, ring_state->cur);
    ring_state->slot = &txring->slot[cur];
    ring_state->dst = (uint8_t *)NETMAP_BUF(txring, txring->slot[cur].buf_idx);
}

static inline void aps_inc_video(struct audio_packet_state *aps)
{
    aps->video_counter += 1;
}

static inline bool aps_audio_needed(struct audio_packet_state *aps)
{
    return aps->video_counter == aps->video_limit;
}

static inline void aps_inc_audio(struct audio_packet_state *aps)
{
    aps->video_counter = 0;
    aps->video_limit = (aps->audio_counter + 1) * aps->num / aps->den - (aps->audio_counter) * aps->num / aps->den;
    aps->audio_counter = (aps->audio_counter + 1) % aps->den;
}

static void make_fake_packet(struct ring_state *ring_state, const void *header,
        uint16_t length, uint16_t header_len)
{
    memset(ring_state->dst, 0, length);
    memcpy(ring_state->dst, header, header_len);
    ring_state->slot->len = length;
    ring_state->slot->ptr = 0;
}

static void adjust_skew(struct ring_state *ring_state, struct upipe_netmap_intf *intf,
        uint32_t *txavail, uint16_t packet_size)
{
    if (intf->d && intf->up) {
        make_fake_packet(ring_state, intf->fake_header, packet_size, intf->header_len);
        advance_ring_state(ring_state);
        *txavail -= 1;
    }
}

static void upipe_netmap_sink_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    struct upipe_netmap_sink_audio *audio_subpipe = &upipe_netmap_sink->audio_subpipe;

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

    struct ring_state ring_state[2] = {{ 0 }};
    bool up[2] = {false, false};

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (unlikely(!intf->d))
            break;

        ring_state[i].txring = NETMAP_TXRING(intf->d->nifp, intf->ring_idx);

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
            ring_state[i].txring = NULL;
            continue;
        }

        uint32_t t = nm_ring_space(ring_state[i].txring);
        max_slots = ring_state[i].txring->num_slots - 1;

        if (intf->wait) {
            if ((now - intf->wait) > UCLOCK_FREQ) {
                ioctl(NETMAP_FD(intf->d), NIOCTXSYNC, NULL); // update userspace ring
                if (t < max_slots - 32) {
                    upipe_notice_va(upipe, "waiting, %u", t);
                    ring_state[i].txring = NULL;
                    continue;
                }

                if (!ring_state[!i].txring) { // 2nd interface down
                    intf->up = true;
                    intf->wait = 0;
                    break;
                }

                txavail = nm_ring_space(ring_state[!i].txring);

                upipe_clear_queues(upipe, intf, t - txavail, 1);
                ioctl(NETMAP_FD(intf->d), NIOCTXSYNC, NULL); // start emptying 1

                /* Legacy Intel */
#if 0
                // update other NIC
                struct upipe_netmap_intf *intf0 = &upipe_netmap_sink->intf[!i];
                ioctl(NETMAP_FD(intf0->d), NIOCTXSYNC, NULL);
                txavail = nm_ring_space(txring[!i]);

                // synchronize within 1024 packets
                upipe_resync_queues(upipe, txring[!i]->num_slots - 1 - txavail - 1024);
                t = nm_ring_space(txring[i]);
                upipe_notice_va(upipe, "tx1 %u", t);

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
#endif

                // we're up
                intf->up = true;
                intf->wait = 0;

                upipe_notice_va(upipe, "RESYNCED (#2), tx0 %u tx1 %u", txavail, t);
            } else {
                ring_state[i].txring = NULL;
                continue;
            }
        }

        uint32_t cur = ring_state[i].cur = ring_state[i].txring->cur;
        ring_state[i].slot = &ring_state[i].txring->slot[cur];
        ring_state[i].dst = (uint8_t *)NETMAP_BUF(ring_state[i].txring, ring_state[i].slot->buf_idx);

        if (txavail > t)
            txavail = t;
    }

    uint32_t num_slots = 0;
    for (size_t i = 0; i < 2; i++) {
        if (ring_state[i].txring) {
            num_slots = ring_state[i].txring->num_slots;
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

        /* FIXME: Is this ok? */
        upipe_netmap_sink->uref = NULL;

        /* Drop associated sound uref */
        if (audio_subpipe->uref) {
            uref_sound_unmap(audio_subpipe->uref, 0, -1, 1);
            uref_free(audio_subpipe->uref);
            audio_subpipe->uref = NULL;
        }

        upipe_netmap_sink_reset_counters(upipe);
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

    uint64_t t;
    if (ring_state[0].slot)
        t = UINT64_LOW_MASK(ring_state[0].slot->ptr) / 1000 * 27;
    else
        t = UINT64_LOW_MASK(ring_state[1].slot->ptr) / 1000 * 27;

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
    else {
        for (size_t i = 0; i < 2; i++) {
            struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
            if (!intf->d)
                continue;
            upipe_clear_queues(upipe, intf, txavail, 0);
        }
   }

    const bool progressive = upipe_netmap_sink->progressive;

    bool adjust_skew_done = false;
    /* fill ring buffer */
    while (txavail) {
        /* Audio insertion/multiplex. */
        if (rfc4175 && aps_audio_needed(&upipe_netmap_sink->audio_packet_state)) {
            struct upipe_netmap_sink_audio *audio_subpipe = &upipe_netmap_sink->audio_subpipe;
            struct upipe *subpipe = upipe_netmap_sink_audio_to_upipe(audio_subpipe);

            if (txavail < audio_subpipe->num_flows)
                break;

            /* Get uref and map data. */
            bool have_audio = ubase_check(get_audio(audio_subpipe));

            if (have_audio) {
                pack_audio(audio_subpipe);
            } else {
                /* TODO: print exact error? */
                upipe_dbg(subpipe, "No audio available, outputting silence");
                memset(audio_subpipe->audio_data, 0, sizeof audio_subpipe->audio_data);
            }

            uint16_t packet_size = upipe_netmap_sink->intf[0].header_len + audio_subpipe->payload_size;
            for (int flow = 0; flow < audio_subpipe->num_flows; flow++) {
                int channel_offset = flow * audio_subpipe->output_channels;

                check_marker_packet(upipe_netmap_sink, ring_state);

                for (size_t i = 0; i < 2; i++) {
                    uint8_t *dst = ring_state[i].dst;
                    if (!dst)
                        continue;

                    struct aes67_flow *aes67_flow = &audio_subpipe->flows[flow][i];

                        /* Copy headers. */
                        memcpy(dst, aes67_flow->header, upipe_netmap_sink->intf[i].header_len);
                        dst += upipe_netmap_sink->intf[i].header_len;
                        memcpy(dst, upipe_netmap_sink->audio_rtp_header, RTP_HEADER_SIZE);
                        dst += RTP_HEADER_SIZE;
                        /* Copy payload. */
                        audio_copy_samples_to_packet(dst, audio_subpipe->audio_data,
                                audio_subpipe->output_channels,
                                audio_subpipe->output_samples,
                                channel_offset);

                    ring_state[i].slot->len = packet_size;
                    ring_state[i].slot->ptr = 0;
                    advance_ring_state(&ring_state[i]);
                }
            }

            /* If there is not enough audio samples left for a whole
             * frame/packet then cache the rest for use next time. */
            if (have_audio && audio_subpipe->uref_samples < audio_subpipe->output_samples)
                handle_audio_tail(audio_subpipe);

            /* Read current sequence number and timestamp. */
            uint16_t seqnum = rtp_get_seqnum(upipe_netmap_sink->audio_rtp_header);
            uint32_t timestamp = rtp_get_timestamp(upipe_netmap_sink->audio_rtp_header);

            /* Advance sequence number and timestamp for next packet. */
            rtp_set_seqnum(upipe_netmap_sink->audio_rtp_header, seqnum + 1);
            rtp_set_timestamp(upipe_netmap_sink->audio_rtp_header, timestamp + audio_subpipe->output_samples);

            upipe_netmap_sink->bits += 8 * (packet_size + 4/*CRC*/) * audio_subpipe->num_flows;
            txavail -= audio_subpipe->num_flows;

            aps_inc_audio(&upipe_netmap_sink->audio_packet_state);

            if (!txavail)
                break;
        }

        /* Insert fake packets to adjust transmission rate differences. */
        if (upipe_netmap_sink->step && (upipe_netmap_sink->pkts_in_frame % upipe_netmap_sink->step) == 0) {
            check_marker_packet(upipe_netmap_sink, ring_state);

            for (size_t i = 0; i < 2; i++) {
                struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
                if (unlikely(!intf->d || !intf->up))
                    continue;

                make_fake_packet(&ring_state[i], intf->fake_header, upipe_netmap_sink->packet_size, intf->header_len);
                advance_ring_state(&ring_state[i]);
            }
            txavail--;
            upipe_netmap_sink->fakes++;
            upipe_netmap_sink->pkts_in_frame++;
            if (!txavail)
                break;
        }

        /* To adjust skew insert 1 fake packet on 1 ring each time this worker
         * function is called so that the TX rate isn't thrown way off.  Only
         * start doing this after real timestamps have been used. */
        if (unlikely(!adjust_skew_done) && likely(upipe_netmap_sink->frame_ts > 1)) {
            /* If ring 1 is behind ring 0 then delay ring 0 by adding fakes. */
            if (ring_state[1].skew > 2*upipe_netmap_sink->packet_duration) {
                adjust_skew(&ring_state[0], &upipe_netmap_sink->intf[0], &txavail, upipe_netmap_sink->packet_size);
                ring_state[1].skew = 0;
                adjust_skew_done = true;
            }

            /* If ring 0 is behind ring 1 then delay ring 1 by adding fakes. */
            else if (ring_state[1].skew < -2*upipe_netmap_sink->packet_duration) {
                adjust_skew(&ring_state[1], &upipe_netmap_sink->intf[1], &txavail, upipe_netmap_sink->packet_size);
                ring_state[1].skew = 0;
                adjust_skew_done = true;
            }

            if (!txavail)
                break;
        }

        if (!uref) {
            uref = get_uref(upipe);
            if (!uref)
                break;
            input_size = -1;
        }

        if (unlikely(input_size == -1)) {
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

        check_marker_packet(upipe_netmap_sink, ring_state);

        /* TODO: clean this. */
        uint8_t *dst[2] = { NULL, NULL };
        uint16_t *len[2] = { NULL, NULL };
        uint64_t *ptr[2] = { NULL, NULL };

        for (size_t i = 0; i < 2; i++) {
            struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
            if (unlikely(!intf->d || !intf->up))
                continue;

            dst[i] = ring_state[i].dst;
            len[i] = &ring_state[i].slot->len;
            ptr[i] = &ring_state[i].slot->ptr;
            advance_ring_state(&ring_state[i]);
        }

        if (rfc4175) {
            /* At the beginning of a frame or field fill the "gap" with empty packets */
            const bool first_field_or_frame = (upipe_netmap_sink->line == 0 || (!progressive && upipe_netmap_sink->line == upipe_netmap_sink->vsize / 2));
            if (first_field_or_frame && upipe_netmap_sink->pixel_offset == 0 && upipe_netmap_sink->gap_fakes_current) {
                if (upipe_netmap_sink->write_ancillary) {
                    write_ancillary(upipe_netmap_sink, dst, len, ptr);
                    upipe_netmap_sink->write_ancillary = false;
                    /* upipe_netmap_sink->bits incremented in write_ancillary() as packet sizes are variable */
                }
                else {
                    for (size_t i = 0; i < 2; i++) {
                        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
                        if (unlikely(!intf->d || !intf->up))
                            continue;
                        /* Do not use make_fake_packet() here because
                        * advance_ring_state() is called above at the start of the
                        * video handling section. */
                        memset(dst[i], 0, pkt_len);
                        memcpy(dst[i], intf->fake_header, intf->header_len);
                        *len[i] = pkt_len;
                        *ptr[i] = 0;
                    }
                    upipe_netmap_sink->bits += (pkt_len + 4 /* CRC */) * 8;
                }
                upipe_netmap_sink->gap_fakes_current--;
            } else {
                if (!upipe_netmap_sink->gap_fakes_current)
                    setup_gap_fakes(upipe_netmap_sink, progressive);

                if (worker_rfc4175(upipe_netmap_sink, dst, len, ptr)) {
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

                    /* Set the audio timestamps here, having got correct video
                     * timestamps in handle_tx_stamp. However, handle_tx_stamp
                     * occurs somewhere during a frame and therefore audio can't
                     * use it until a clear reference point such as eof (here).
                     *
                     * If the video timestamps have been set to real values then
                     * set the audio timestamp to the video for the start of the
                     * next frame. */
                    if (upipe_netmap_sink->frame_ts_start != 0) {
                        /* TODO: check whether this really needs 128 bit. */
                        __uint128_t ts = upipe_netmap_sink->frame_count;
                        ts *= upipe_netmap_sink->fps.den;
                        ts *= 48000;
                        ts /= upipe_netmap_sink->fps.num;
                        rtp_set_timestamp(upipe_netmap_sink->audio_rtp_header, ts);
                        upipe_netmap_sink->frame_ts_start = 0;
                    }
                }
            }
            aps_inc_video(&upipe_netmap_sink->audio_packet_state);
        } else {
            int s = worker_hbrmt(upipe_netmap_sink, dst, src_buf, bytes_left, len, ptr);
            src_buf += s;
            bytes_left -= s;
            assert(bytes_left >= 0);

            /* 64 bits overflows after 375 years at 1.5G */
            upipe_netmap_sink->bits += (upipe_netmap_sink->packet_size + 4 /* CRC */) * 8;

            if (!bytes_left) {
                uref_block_unmap(uref, 0);
                uref_free(uref);
                uref = NULL;
                upipe_netmap_sink->uref = NULL;
                upipe_netmap_sink->pkt = 0;

                upipe_netmap_sink->frame_count++;
                upipe_netmap_sink->rtp_timestamp[0] += upipe_netmap_sink->frame_duration;

                /* update hbrmt header */
                uint8_t *hbrmt = &upipe_netmap_sink->rtp_header[RTP_HEADER_SIZE];
                smpte_hbrmt_set_frame_count(hbrmt, upipe_netmap_sink->frame_count & UINT8_MAX);
            }
        }

        upipe_netmap_sink->pkts_in_frame++;
        txavail--;
    }

    /* Catch future bugs that come from undeflowing txavail. */
    assert(txavail <= max_slots);

    if (txavail >= max_slots - 32) {
        upipe_netmap_sink_reset_counters(upipe);

        /* FIXME: Is this ok? */
        upipe_netmap_sink->uref = NULL;

        upipe_netmap_sink_clear_queues(upipe);

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
        if (unlikely(!intf->d || !intf->up))
            continue;

        ring_state[i].txring->head = ring_state[i].txring->cur = ring_state[i].cur;
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

    uint64_t systime = 0;
    /* Check and warn for uref without timestamp. */
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &systime))))
        upipe_warn(upipe, "received non-dated buffer");

    if (upipe_netmap_sink->frame_size == 0 || upipe_netmap_sink->audio_subpipe.need_reconfig) {
        upipe_netmap_sink->audio_subpipe.need_reconfig = false;

        if (!upipe_netmap_sink->rfc4175) {
/* XXX
 * circular variable store
 * https://app.asana.com/0/1141488647259340/1201586998881878
 * https://app.asana.com/0/1141488647259340/1201642161098561
 */
            uref_block_size(uref, &upipe_netmap_sink->frame_size);
            upipe_netmap_sink->frame_size = upipe_netmap_sink->frame_size * 5 / 8;
            upipe_netmap_sink->packets_per_frame = (upipe_netmap_sink->frame_size + HBRMT_DATA_SIZE - 1) / HBRMT_DATA_SIZE;
            const uint64_t eth_packet_size = upipe_netmap_sink->intf[0].header_len
                + RTP_HEADER_SIZE + HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE
                + 4 /* ethernet CRC */;

            upipe_netmap_sink->rate = 8 * eth_packet_size * upipe_netmap_sink->packets_per_frame * upipe_netmap_sink->fps.num;
        } else {
/* XXX
 * circular variable store
 * https://app.asana.com/0/1141488647259340/1201642161098561
 */
            uint64_t pixels = upipe_netmap_sink->hsize * upipe_netmap_sink->vsize;
            upipe_netmap_sink->frame_size = pixels * UPIPE_RFC4175_PIXEL_PAIR_BYTES / 2;
            /* Length of all network headers apart from payload */
            const uint16_t network_header_len = upipe_netmap_sink->intf[0].header_len
                + RTP_HEADER_SIZE + RFC_4175_HEADER_LEN + RFC_4175_EXT_SEQ_NUM_LEN;
            const uint16_t bytes_available = upipe_netmap_sink->packet_size - network_header_len;
            const uint64_t payload = (bytes_available / UPIPE_RFC4175_PIXEL_PAIR_BYTES) * UPIPE_RFC4175_PIXEL_PAIR_BYTES;
            upipe_netmap_sink->payload = payload;

            upipe_netmap_sink->packets_per_frame = (upipe_netmap_sink->frame_size + payload - 1) / payload;
            uint64_t actual_packets_per_frame = upipe_netmap_sink->packets_per_frame + upipe_netmap_sink->gap_fakes;

            upipe_netmap_sink->rate = 8 * (actual_packets_per_frame * (network_header_len + payload + 4 /* CRC */)) * upipe_netmap_sink->fps.num;

            struct upipe_netmap_sink_audio *audio_subpipe = upipe_netmap_sink_to_audio_subpipe(upipe_netmap_sink);
            const uint64_t audio_pps = (48000 / audio_subpipe->output_samples) * audio_subpipe->num_flows;
            const uint64_t audio_bitrate = 8 * (upipe_netmap_sink->intf[0].header_len + audio_subpipe->payload_size + 4/*CRC*/) * audio_pps;
            upipe_dbg_va(upipe, "audio bitrate %"PRIu64" video bitrate %"PRIu64" \n", audio_bitrate, upipe_netmap_sink->rate);
            upipe_netmap_sink->rate += audio_bitrate * upipe_netmap_sink->fps.den;

            /* Video will have (packets_per_frame * fps) packets per second.
             * Audio needs to output (48000/6) packets per second.  The ratio
             * between these two is calculated with the rational below.  Need to
             * account for the gap fakes too. */
            struct urational rational = {
                (actual_packets_per_frame) * upipe_netmap_sink->fps.num,
                (48000 / audio_subpipe->output_samples) * upipe_netmap_sink->fps.den
            };
            urational_simplify(&rational);
            upipe_netmap_sink->audio_packet_state = (struct audio_packet_state) {
                .num = rational.num, .den = rational.den,
                .video_limit = rational.num / rational.den,
            };
            upipe_dbg_va(upipe, "rational: %"PRId64"/%"PRIu64, rational.num, rational.den);
        }
        /* Time of 1 packet in nanoseconds. */
        upipe_netmap_sink->packet_duration = UINT64_C(1000000000) * upipe_netmap_sink->fps.den
            / (upipe_netmap_sink->fps.num * (upipe_netmap_sink->packets_per_frame + upipe_netmap_sink->gap_fakes));

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
                double tx_rate_factor = 1;
                if (!upipe_netmap_sink->rfc4175)
                    tx_rate_factor = upipe_netmap_sink->tx_rate_factor;
                double tx_rate = tx_rate_factor
                    * upipe_netmap_sink->rate
                    / upipe_netmap_sink->fps.den;
                fprintf(f, "%.0f", tx_rate);
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
/* XXX
 * circular variable store
 * https://app.asana.com/0/1141488647259340/1201586998881877
 * https://app.asana.com/0/1141488647259340/1201586998881878
 * https://app.asana.com/0/1141488647259340/1201642161098561
 * sharpen responsibilities of public functions
 * https://app.asana.com/0/1141488647259340/1201639915090001
 */
static int upipe_netmap_sink_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);

    uint16_t udp_payload_size;

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

        /* Just the headers, packed data added below. */
        udp_payload_size = RTP_HEADER_SIZE + RFC_4175_HEADER_LEN + RFC_4175_EXT_SEQ_NUM_LEN;
    } else {
        upipe_netmap_sink->rfc4175 = 0;
        udp_payload_size = RTP_HEADER_SIZE + HBRMT_HEADER_SIZE + HBRMT_DATA_SIZE;
    }

    UBASE_RETURN(uref_pic_flow_get_hsize(flow_def, &upipe_netmap_sink->hsize));
    UBASE_RETURN(uref_pic_flow_get_vsize(flow_def, &upipe_netmap_sink->vsize));
    upipe_netmap_sink->progressive = ubase_check(uref_pic_get_progressive(flow_def));

/* XXX
 * 2110 (rf4175) packet size decisions
 * https://app.asana.com/0/1141488647259340/1201586998881877
 */
    if (upipe_netmap_sink->hsize == 720) {
        if (upipe_netmap_sink->rfc4175)
            udp_payload_size += 720/2*5 / 2;
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
            udp_payload_size += 1920/2*5 / 4;
            upipe_netmap_sink->gap_fakes = 4 * (1125 - 1080);
        }
        upipe_netmap_sink->frame = upipe_netmap_sink->progressive ? 0x21 : 0x20;
        // XXX: should we do PSF at all?
        // 0x22 psf
    } else if (upipe_netmap_sink->hsize == 1280 && upipe_netmap_sink->vsize == 720) {
        if (upipe_netmap_sink->rfc4175) {
            udp_payload_size += 1280/2*5 / 4;
            upipe_netmap_sink->gap_fakes = 4 * (750 - 720);
        }
        upipe_netmap_sink->frame = 0x30; // progressive
    } else
        return UBASE_ERR_INVALID;

    /* setup gap_fakes_current for the first frame */
    if (upipe_netmap_sink->rfc4175)
        setup_gap_fakes(upipe_netmap_sink, upipe_netmap_sink->progressive);
    /* otherwise ensure both gap fakes are set to 0 */
    else
        upipe_netmap_sink->gap_fakes = upipe_netmap_sink->gap_fakes_current = 0;

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

/* XXX
 * ip header creation duplicate
 * https://app.asana.com/0/1141488647259340/1201642161098564
 * circular variable store
 * https://app.asana.com/0/1141488647259340/1201642161098561
 */
    /* Create ethernet, IP, and UDP headers and set the packet size. */
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d)
            break;
        intf->header_len = upipe_netmap_put_ip_headers(intf, intf->header, udp_payload_size);
    }
    if (upipe_netmap_sink->intf[0].header_len && upipe_netmap_sink->intf[1].header_len)
        assert(upipe_netmap_sink->intf[0].header_len == upipe_netmap_sink->intf[1].header_len);
    upipe_netmap_sink->packet_size = upipe_netmap_sink->intf[0].header_len + udp_payload_size;

    if (!upipe_netmap_sink->rfc4175) {
        /* Largely constant headers so don't keep rewriting them */
        upipe_netmap_put_rtp_headers(upipe_netmap_sink, upipe_netmap_sink->rtp_header,
                false, 98, upipe_netmap_sink->seqnum, false, false);
        upipe_put_hbrmt_headers(upipe, upipe_netmap_sink->rtp_header + RTP_HEADER_SIZE);
    } else {
            /* RTP Headers done in worker_rfc4175 */
        upipe_netmap_update_timestamp_cache(upipe_netmap_sink);

        /* RTP header for audio. */
        memset(upipe_netmap_sink->audio_rtp_header, 0, RTP_HEADER_SIZE);
        rtp_set_hdr(upipe_netmap_sink->audio_rtp_header);
        rtp_set_type(upipe_netmap_sink->audio_rtp_header, upipe_netmap_sink->rtp_pt_audio);
        rtp_set_seqnum(upipe_netmap_sink->audio_rtp_header, 0);
        rtp_set_timestamp(upipe_netmap_sink->audio_rtp_header, 0);
    }

    upipe_netmap_sink->frame_size = 0;
    upipe_netmap_sink_reset_counters(upipe);
    upipe_netmap_sink->uref = NULL;

    upipe_netmap_sink_clear_queues(upipe);

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

/* XXX
 * make audio and video use same parser
 * https://app.asana.com/0/1141488647259340/1201639915089998
 */
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

    if (!ip || strlen(ip) == 0) {
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

    /* TODO: consider replacing this with one of inet_aton(), inet_pton(), or
     * getaddrinfo() because an error will result in -1 being returned. */
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

/* XXX
 * circular variable store
 * https://app.asana.com/0/1141488647259340/1201586998881877
 * https://app.asana.com/0/1141488647259340/1201586998881878
 * https://app.asana.com/0/1141488647259340/1201642161098561
 * sharpen responsibilities of public functions
 * https://app.asana.com/0/1141488647259340/1201639915090001
 * ip header creation duplicate
 * https://app.asana.com/0/1141488647259340/1201642161098564
 */
static int upipe_netmap_sink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_netmap_sink *upipe_netmap_sink =
        upipe_netmap_sink_from_upipe(upipe);
    int ret;
    /* Needs to be declared before the goto. */
    uint16_t udp_payload_size = upipe_netmap_sink->packet_size - upipe_netmap_sink->intf[0].header_len;

    upipe_netmap_sink_set_upump(upipe, NULL);
    upipe_netmap_sink_check_upump_mgr(upipe);
    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE; /* FIXME: does this need some headers? */

    upipe_netmap_sink->uri = strdup(uri);
    if (unlikely(upipe_netmap_sink->uri == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        ret = UBASE_ERR_ALLOC;
        goto error_headers_needed;
    }

    char *p = strchr(upipe_netmap_sink->uri, '+');
    if (p)
        *p++ = '\0';

/* XXX
 * make audio and video use same parser
 * https://app.asana.com/0/1141488647259340/1201639915089998
 */
    struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[0];
    ret = upipe_netmap_sink_ip_params(upipe, intf, upipe_netmap_sink->uri);
    if (!ubase_check(ret))
        goto error_headers_needed;

    if (p) {
        p[-1] = '+';
        ret = upipe_netmap_sink_ip_params(upipe, intf+1, p);
        if (!ubase_check(ret))
            goto error_headers_needed;
    }

    /* Create ethernet, IP, and UDP headers. */
    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d)
            break;
        intf->header_len = upipe_netmap_put_ip_headers(intf, intf->header, udp_payload_size);
    }
    if (upipe_netmap_sink->intf[0].header_len && upipe_netmap_sink->intf[1].header_len)
        assert(upipe_netmap_sink->intf[0].header_len == upipe_netmap_sink->intf[1].header_len);
    upipe_netmap_sink->packet_size = upipe_netmap_sink->intf[0].header_len + udp_payload_size;

    return UBASE_ERR_NONE;

error_headers_needed:

    /* An error has happened so to ensure this pipe doesn't send packets with an
     * old or invalid header copy the source details over the destination
     * details. */

    for (size_t i = 0; i < 2; i++) {
        struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[i];
        if (!intf->d)
            break;
        memcpy(intf->dst_mac, intf->src_mac, ETHERNET_ADDR_LEN);
        intf->dst_ip   = intf->src_ip;
        intf->dst_port = intf->src_port = (intf->ring_idx+1) * 1000;
        intf->header_len = upipe_netmap_put_ip_headers(intf, intf->header, udp_payload_size);
    }
    if (upipe_netmap_sink->intf[0].header_len && upipe_netmap_sink->intf[1].header_len)
        assert(upipe_netmap_sink->intf[0].header_len == upipe_netmap_sink->intf[1].header_len);
    upipe_netmap_sink->packet_size = upipe_netmap_sink->intf[0].header_len + udp_payload_size;

    return ret;
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
    /* FIXME: this uref is leaked. */
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

static int upipe_netmap_set_option(struct upipe *upipe, const char *option,
        const char *value)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);

    if (!option || !value)
        return UBASE_ERR_INVALID;

    if (!strcmp(option, "rtp-pt-video")) {
        int type = atoi(value);
        if (type < 0 || type > 127) {
            upipe_err_va(upipe, "rtp-pt-video value (%d) out of range 0..127", type);
            return UBASE_ERR_INVALID;
        }
        upipe_netmap_sink->rtp_pt_video = type;
        return UBASE_ERR_NONE;
    }

    if (!strcmp(option, "rtp-pt-audio")) {
        int type = atoi(value);
        if (type < 0 || type > 127) {
            upipe_err_va(upipe, "rtp-pt-audio value (%d) out of range 0..127", type);
            return UBASE_ERR_INVALID;
        }
        upipe_netmap_sink->rtp_pt_audio = type;
        /* FIXME: remove this after cleaning up how headers are handled.  IP
         * details have (had) a similar issue about not updating. */
        rtp_set_type(upipe_netmap_sink->audio_rtp_header, type);
        return UBASE_ERR_NONE;
    }

    if (!strcmp(option, "rtp-pt-ancillary")) {
        int type = atoi(value);
        if (type < 0 || type > 127) {
            upipe_err_va(upipe, "rtp-pt-ancillary value (%d) out of range 0..127", type);
            return UBASE_ERR_INVALID;
        }
        upipe_netmap_sink->rtp_pt_ancillary = type;
        /* FIXME: remove this after cleaning up how headers are handled.  IP
         * details have (had) a similar issue about not updating. */
        rtp_set_type(upipe_netmap_sink->ancillary_rtp_header, type);
        return UBASE_ERR_NONE;
    }

    upipe_err_va(upipe, "Unknown option %s", option);
    return UBASE_ERR_INVALID;
}

static int ancillary_set_destination(struct upipe * upipe,
        const char *path_1, const char *path_2);

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
            upipe_netmap_sink->uref = NULL;

            upipe_netmap_sink_clear_queues(upipe);

            upipe_netmap_sink_set_upump(upipe, NULL);
            return upipe_netmap_sink_set_uri(upipe, uri);
        }

        case UPIPE_NETMAP_SINK_GET_AUDIO_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_NETMAP_SINK_SIGNATURE)
            upipe_dbg(upipe, "UPIPE_NETMAP_SINK_GET_AUDIO_SUB");
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p =  upipe_netmap_sink_audio_to_upipe(
                    upipe_netmap_sink_to_audio_subpipe(
                        upipe_netmap_sink_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value  = va_arg(args, const char *);
            return upipe_netmap_set_option(upipe, option, value);
        }

        case UPIPE_NETMAP_SINK_ANCILLARY_SET_DESTINATION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_NETMAP_SINK_SIGNATURE)
            const char *path_1 = va_arg(args, const char *);
            const char *path_2 = va_arg(args, const char *);
            return ancillary_set_destination(upipe, path_1, path_2);
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
        free(intf->device_base_name);
        nm_close(intf->d);
        close(intf->fd);
    }

    upipe_netmap_sink_clear_queues(upipe);

    upipe_netmap_sink_clean_upump(upipe);
    upipe_netmap_sink_clean_upump_mgr(upipe);
    upipe_netmap_sink_clean_urefcount(upipe);
    upipe_netmap_sink_clean_uclock(upipe);
    upipe_clean(&upipe_netmap_sink->audio_subpipe.upipe);
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

/*
 * Audio subpipe.
 */

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_netmap_sink_audio_set_flow_def(struct upipe *upipe,
        struct uref *flow_def)
{
    struct upipe_netmap_sink_audio *audio_subpipe = upipe_netmap_sink_audio_from_upipe(upipe);
    upipe_dbg_va(upipe, "%s", __func__);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "sound.s32."))

    /* Clear buffered urefs. */
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&audio_subpipe->urefs, uchain, uchain_tmp) {
        ulist_delete(uchain);
        uref_free(uref_from_uchain(uchain));
    }
    audio_subpipe->n = 0;

    /* Check for flow_def and get latency attribute. */
    if (unlikely(ubase_check(uref_flow_get_def(flow_def, NULL)))) {
        uint64_t latency = 0;
        uref_clock_get_latency(flow_def, &latency);
        audio_subpipe->latency = latency;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This handles input data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_netmap_sink_audio_input(struct upipe *upipe,
        struct uref *uref, struct upump **upump_p)
{
    struct upipe_netmap_sink_audio *audio_subpipe = upipe_netmap_sink_audio_from_upipe(upipe);
    uint64_t systime = 0;

    /* Check and warn for uref without timestamp. */
    if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &systime))))
        upipe_warn(upipe, "received non-dated buffer");

    ulist_add(&audio_subpipe->urefs, uref_to_uchain(uref));
    audio_subpipe->n += 1;
    upipe_dbg_va(upipe, "%s: %"PRIu64, __func__, audio_subpipe->n);

    if (audio_subpipe->n > MAX_AUDIO_UREFS) {
        /* Clear buffered urefs. */
        struct uchain *uchain, *uchain_tmp;
        ulist_delete_foreach(&audio_subpipe->urefs, uchain, uchain_tmp) {
            ulist_delete(uchain);
            uref_free(uref_from_uchain(uchain));
        }
        audio_subpipe->n = 0;
    }
}

static int audio_set_flow_destination(struct upipe * upipe, int flow,
        const char *path_1, const char *path_2);
static int audio_subpipe_set_option(struct upipe *upipe, const char *option,
        const char *value);

/** @internal @This processes control commands on a subpipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_netmap_sink_audio_control(struct upipe *upipe,
                                         int command, va_list args)
{
    upipe_dbg_va(upipe, "%s", __func__);
    UBASE_HANDLED_RETURN(upipe_control_provide_request(upipe, command, args));

    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_netmap_sink_audio_set_flow_def(upipe, flow_def);
    }

    case UPIPE_SET_OPTION: {
        const char *option = va_arg(args, const char *);
        const char *value  = va_arg(args, const char *);
        return audio_subpipe_set_option(upipe, option, value);
    }

    case UPIPE_NETMAP_SINK_AUDIO_SET_FLOW_DESTINATION: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_NETMAP_SINK_AUDIO_SIGNATURE)
        int flow = va_arg(args, int);
        const char *path_1 = va_arg(args, const char *);
        const char *path_2 = va_arg(args, const char *);
        return audio_set_flow_destination(upipe, flow, path_1, path_2);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

static struct upipe_mgr upipe_netmap_sink_audio_mgr = {
    .signature = UPIPE_NETMAP_SINK_AUDIO_SIGNATURE,

    .upipe_control = upipe_netmap_sink_audio_control,
    .upipe_input = upipe_netmap_sink_audio_input,
};

static int get_audio(struct upipe_netmap_sink_audio *audio_subpipe)
{
    /* If audio data is already mapped then return early. */
    if (audio_subpipe->uref)
        return UBASE_ERR_NONE;

    /* Get uref from buffer and return an error if none available. */
    //struct upipe *upipe = upipe_netmap_sink_audio_to_upipe(audio_subpipe);
    struct uchain *uchain = ulist_pop(&audio_subpipe->urefs);
    UBASE_ALLOC_RETURN(uchain);
    audio_subpipe->n -= 1;
    struct uref *uref = uref_from_uchain(uchain);

    /* Check size. */
    size_t samples = 0;
    uint8_t channels = 0;
    UBASE_RETURN(uref_sound_size(uref, &samples, &channels));
    channels /= 4;

    /* Map uref. */
    const int32_t *src = NULL;
    UBASE_RETURN(uref_sound_read_int32_t(uref, 0, -1, &src, 1));

    /* Add any cached samples. */
    samples += audio_subpipe->cached_samples;
    /* Rewind source pointer for any cached samples. */
    src -= audio_subpipe->cached_samples * channels;

    audio_subpipe->uref = uref;
    audio_subpipe->data = src;
    audio_subpipe->channels = channels;
    audio_subpipe->uref_samples = samples;

    return UBASE_ERR_NONE;
}

#define bswap32 __builtin_bswap32

static void pack_audio(struct upipe_netmap_sink_audio *audio_subpipe)
{
    const int32_t *src = audio_subpipe->data;
    uint8_t *dst = audio_subpipe->audio_data;
    const int start = audio_subpipe->cached_samples * audio_subpipe->channels;
    const int end = audio_subpipe->output_samples * audio_subpipe->channels;

    for (int j = start; j < end; j++) {
        int32_t sample = src[j];
        uint32_t *dst32 = (uint32_t*)&dst[3*j];
        *dst32 = bswap32(sample);
    }

    if (audio_subpipe->cached_samples)
        audio_subpipe->cached_samples = 0;

    audio_subpipe->data += end;
    audio_subpipe->uref_samples -= audio_subpipe->output_samples;
}

#undef bswap32

static void handle_audio_tail(struct upipe_netmap_sink_audio *audio_subpipe)
{
    const int32_t *src = audio_subpipe->data;
    uint8_t *dst = audio_subpipe->audio_data;

    /* Pack tail of uref into buffer. */
    for (int j = 0;
            j < audio_subpipe->uref_samples * audio_subpipe->channels;
            j++) {
        int32_t sample = src[j];
        dst[3*j+0] = (sample >> 24) & 0xff;
        dst[3*j+1] = (sample >> 16) & 0xff;
        dst[3*j+2] = (sample >>  8) & 0xff;
    }
    audio_subpipe->cached_samples = audio_subpipe->uref_samples;

    uref_sound_unmap(audio_subpipe->uref, 0, -1, 1);
    uref_free(audio_subpipe->uref);
    audio_subpipe->uref = NULL;
    audio_subpipe->uref_samples = 0;
}

static inline int audio_count_populated_flows(const struct upipe_netmap_sink_audio *audio_subpipe)
{
    int ret = 0;
    for (int i = 0; i < AES67_MAX_FLOWS; i++)
        ret += audio_subpipe->flows[i][0].populated;
    if (ret > 16 / audio_subpipe->output_channels)
        ret = 16 / audio_subpipe->output_channels;
    return ret;
}

static int audio_set_flow_destination(struct upipe * upipe, int flow,
        const char *path_1, const char *path_2)
{
    struct upipe_netmap_sink_audio *audio_subpipe = upipe_netmap_sink_audio_from_upipe(upipe);
    const struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_audio_subpipe(audio_subpipe);

    if (unlikely(flow < 0 || flow >= AES67_MAX_FLOWS)) {
        upipe_err_va(upipe, "flow %d is not in the range 0..%d", flow, AES67_MAX_FLOWS-1);
        return UBASE_ERR_INVALID;
    }

    struct aes67_flow *aes67_flow = audio_subpipe->flows[flow];
    const struct upipe_netmap_intf *intf = &upipe_netmap_sink->intf[0];

    /* If given NULL or 0-length strings on both arguments it indicates a reset
     * for the destination information. */
    if ((path_1 == NULL && path_2 == NULL)
            || (strlen(path_1) == 0 && strlen(path_2) == 0)) {
        aes67_flow[0].populated = false;
        aes67_flow[1].populated = false;
        audio_subpipe->num_flows = audio_count_populated_flows(audio_subpipe);
        memset(aes67_flow[0].header, 0, sizeof aes67_flow[0].header);
        memset(aes67_flow[1].header, 0, sizeof aes67_flow[1].header);
        return UBASE_ERR_NONE;
    }

/* XXX
 * make audio and video use same parser
 * https://app.asana.com/0/1141488647259340/1201639915089998
 */

    int ret = parse_destinations(upipe, &aes67_flow[0].dest, &aes67_flow[1].dest,
            path_1, path_2);
    const struct destination *dst[2] = { &aes67_flow[0].dest, &aes67_flow[1].dest };
    if (!ubase_check(ret)) {
        /* The master_enable=false setting passes ":port" which return an error
         * in parsing so it needs special handling. */
        if (path_1[0] == ':' && path_2[0] == ':') {
            dst[0] = &intf[0].source;
            dst[1] = &intf[1].source;
            ret = UBASE_ERR_NONE;
        }

        else {
        upipe_err_va(upipe, "error parsing '%s' and '%s': %s (%d)",
                path_1, path_2, ubase_err_str(ret), ret);
        /* TODO: change/reset something on error? */
        return ret;
        }
    }

    for (int i = 0; i < 2; i++) {
        make_header(aes67_flow[i].header, &intf[i].source, dst[i],
                intf[i].vlan_id, audio_subpipe->payload_size);
    }

    aes67_flow[0].populated = true;
    aes67_flow[1].populated = true;
    audio_subpipe->num_flows = audio_count_populated_flows(audio_subpipe);
    audio_subpipe->need_reconfig = true;
    return ret;
}

static int ancillary_set_destination(struct upipe * upipe, const char *path_1, const char *path_2)
{
    struct upipe_netmap_sink *upipe_netmap_sink = upipe_netmap_sink_from_upipe(upipe);
    struct upipe_netmap_intf *intf = upipe_netmap_sink->intf;

    int ret = parse_destinations(upipe, &intf[0].ancillary_dest, &intf[1].ancillary_dest,
            path_1, path_2);
    if (!ubase_check(ret)) {
        upipe_err_va(upipe, "error parsing '%s' and '%s': %s (%d)",
                path_1, path_2, ubase_err_str(ret), ret);
        /* TODO: change/reset something on error? */
        return ret;
    }

    for (int i = 0; i < 2; i++) {
        make_header(intf[i].ancillary_header, &intf[i].source, &intf[i].ancillary_dest,
                intf[i].vlan_id, 123 /* fake */);
        /* Ancillary packets are variable size so we can't populate IP/UDP headers*/
    }

    return ret;
}

static inline uint16_t audio_payload_size(uint16_t channels, uint16_t samples)
{
    return RTP_HEADER_SIZE + channels * samples * 3 /*bytes per sample*/;
}

static int audio_subpipe_set_option(struct upipe *upipe, const char *option,
        const char *value)
{
    struct upipe_netmap_sink_audio *audio_subpipe = upipe_netmap_sink_audio_from_upipe(upipe);

    if (!option || !value)
        return UBASE_ERR_INVALID;

    if (!strcmp(option, "output-samples")) {
        int output_samples = atoi(value);
        if (output_samples <= 0 || output_samples > AES67_MAX_SAMPLES_PER_PACKET) {
            upipe_err_va(upipe, "output-samples (%d) not in range 0..%d",
                    output_samples, AES67_MAX_SAMPLES_PER_PACKET);
            return UBASE_ERR_INVALID;
        }

        /* A sample packs to 3 bytes.  16 channels. */
        int needed_size = audio_payload_size(audio_subpipe->output_channels, output_samples);
        if (needed_size > audio_subpipe->mtu) {
            upipe_err_va(upipe, "requested frame or packet size (%d bytes, %d samples) is greater than MTU (%d)",
                    needed_size, output_samples, audio_subpipe->mtu);
            return UBASE_ERR_INVALID;
        }

        audio_subpipe->output_samples = output_samples;
        audio_subpipe->payload_size = needed_size;
        audio_subpipe->need_reconfig = true;
        return UBASE_ERR_NONE;
    }

    if (!strcmp(option, "output-channels")) {
        int output_channels = atoi(value);
        if (!(output_channels == 2 || output_channels == 4 || output_channels == 8 || output_channels == 16)) {
            upipe_err_va(upipe, "output-channels (%d) not 2, 4, 8, or 16", output_channels);
            return UBASE_ERR_INVALID;
        }
        audio_subpipe->output_channels = output_channels;
        audio_subpipe->payload_size = audio_payload_size(output_channels, audio_subpipe->output_samples);
        audio_subpipe->num_flows = audio_count_populated_flows(audio_subpipe);
        audio_subpipe->need_reconfig = true;
        return UBASE_ERR_NONE;
    }

    if (!strcmp(option, "rtp-type-audio")) {
        /* Redirect into the main pipe */
        return upipe_netmap_set_option(upipe_netmap_sink_to_upipe(upipe_netmap_sink_from_audio_subpipe(audio_subpipe)),
                option, value);
    }

    upipe_err_va(upipe, "Unknown option %s", option);
    return UBASE_ERR_INVALID;
}

static inline void audio_copy_samples_to_packet(uint8_t *dst, const uint8_t *src,
        int output_channels, int output_samples, int channel_offset)
{
    /* Slight optimization for single flow. */
    if (output_channels == 16) {
        memcpy(dst, src, output_samples * 16 * 3);
    } else {
        int sample_size = 3 * output_channels;
        src += 3*channel_offset;
        for (int i = 0; i < output_samples; i++) {
            memcpy(dst + i * sample_size, src + 3*16*i, sample_size);
        }
    }
}

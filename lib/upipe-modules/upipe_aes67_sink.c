/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
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
 * @short Upipe sink module for aes67
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <ifaddrs.h>
#include <net/if_arp.h>

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_aes67_sink.h>
#include "upipe_udp.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include <bitstream/ietf/rtp.h>

/** tolerance for late packets */
#define SYSTIME_TOLERANCE UCLOCK_FREQ
/** print late packets */
#define SYSTIME_PRINT (UCLOCK_FREQ / 100)
/** expected flow definition on all flows */
#define EXPECTED_FLOW_DEF    "sound."

#define UDP_DEFAULT_TTL 0
#define UDP_DEFAULT_PORT 1234

#define AES67_MAX_PATHS 2
#define AES67_MAX_FLOWS 8

#define TRANSMISSION_UNIT_SIZE(payload) \
    (ETH_HLEN + RAW_HEADER_SIZE + RTP_HEADER_SIZE + payload)
#define MAX_SAMPLES_PER_PACKET 48
#undef MMAP_FRAME_SIZE
#define MMAP_FRAME_SIZE (TPACKET_ALIGN(sizeof(struct tpacket_hdr) + RAW_HEADER_SIZE + RTP_HEADER_SIZE + MAX_SAMPLES_PER_PACKET * 16 * 3))

#ifndef MTU
#define MTU 1500
#endif

#define STEREO_FLOWS 1

/** @hidden */
static bool upipe_aes67_sink_output(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p, int flow, int path, int channel_offset,
        int output_channels);

struct aes67_flow {
    /* IP details for the destination. */
    struct sockaddr_in sin;
    /* Ethernet details for the destination. */
    struct sockaddr_ll sll;
    /* Raw IP and UDP header. */
    uint8_t raw_header[RAW_HEADER_SIZE];
};

/** @internal @This is the private context of a aes67 sink pipe. */
struct upipe_aes67_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** delay applied to systime attribute when uclock is provided */
    uint64_t latency;
    /** file descriptor */
    int fd[2];

    /* Mapped space for TX ring of frames/packets. */
    void *mmap[2];
    /* Counter for which frame is next ot be used. */
    int mmap_frame_num;

    /* RTP timestamp. */
    uint32_t timestamp;
    /* RTP sequence number. */
    uint16_t seqnum;
    /* Cached audio data (packed) from tails of input urefs. */
    uint8_t audio_data[MAX_SAMPLES_PER_PACKET * 16 * 3];
    /* Number of samples in buffer. */
    int cached_samples;

    /* Input urefs. */
    struct uchain ulist;

    /* Subthread. */
    pthread_t pt;
    /* Mutex for passing urefs and stopping. */
    pthread_mutex_t mutex;
    bool thread_created;
    bool stop;

    /** maximum samples to put in each packet */
    int output_samples;
    /* Number of channels in each flow. */
    int output_channels;
    /* Maximum transmission unit. */
    int mtu;

    /* Interface names. */
    char *ifname[2];
    /* IP details for the source. */
    struct sockaddr_in sin[2];
    /* Ethernet details for the source. */
    struct sockaddr_ll sll[2];
    /* Details for all destinations. */
    struct aes67_flow flows[AES67_MAX_FLOWS][AES67_MAX_PATHS];

    /** public upipe structure */
    struct upipe upipe;
};

/*
   Frame structure:

   - Start. Frame must be aligned to TPACKET_ALIGNMENT=16
   - struct tpacket_hdr
   - pad to TPACKET_ALIGNMENT=16
   - struct sockaddr_ll
   - Gap, chosen so that packet data (Start+tp_net) alignes to TPACKET_ALIGNMENT=16
   - Start+tp_mac: [ Optional MAC header ]
   - Start+tp_net: Packet data, aligned to TPACKET_ALIGNMENT=16.
   - Pad to align to TPACKET_ALIGNMENT=16
*/

#ifndef __aligned_tpacket
# define __aligned_tpacket	__attribute__((aligned(TPACKET_ALIGNMENT)))
#endif

#ifndef __align_tpacket
# define __align_tpacket(x)	__attribute__((aligned(TPACKET_ALIGN(x))))
#endif

union frame_map {
    struct v1 {
        struct tpacket_hdr tph __attribute__((aligned(TPACKET_ALIGNMENT)));
        //struct sockaddr_ll sll __attribute__((aligned(TPACKET_ALIGNMENT)));
        uint8_t data[] /*__attribute__((aligned(TPACKET_ALIGNMENT)))*/;
    } *v1;
    void *raw;
};

UPIPE_HELPER_UPIPE(upipe_aes67_sink, upipe, UPIPE_AES67_SINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_aes67_sink, urefcount, upipe_aes67_sink_free)
UPIPE_HELPER_VOID(upipe_aes67_sink)
UPIPE_HELPER_UCLOCK(upipe_aes67_sink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

static void *run_thread(void *upipe_pointer)
{
    struct upipe *upipe = upipe_pointer;
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);
    struct uref *uref = NULL;
    struct uchain *uchain = NULL;
    uint64_t expected_systime = 0;
    uint32_t expected_timestamp = 0;

    /* Run until told to stop. */
    while (true) {
        pthread_mutex_lock(&upipe_aes67_sink->mutex);
        if (upipe_aes67_sink->stop) {
            pthread_mutex_unlock(&upipe_aes67_sink->mutex);
            break;
        }
        uchain = ulist_pop(&upipe_aes67_sink->ulist);
        pthread_mutex_unlock(&upipe_aes67_sink->mutex);

        /* If no uref in queue, sleep 5us and continue. */
        if (uchain == NULL) {
#if 0
            usleep(5);
#else
            struct timespec wait = { .tv_nsec = 5000 };
            nanosleep(&wait, NULL);
#endif
            continue;
        }
        uref = uref_from_uchain(uchain);

        /* TODO: check uclock exists, and fds are open. */

        /* Get output time. */
        uint64_t systime = 0;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
            upipe_warn(upipe, "received non-dated buffer");
        }
        systime += upipe_aes67_sink->latency;
        /* Offset the systime to start in the right place. */
        systime -= UCLOCK_FREQ * upipe_aes67_sink->cached_samples / 48000;
        /* Handle NTSC PTS jitter. */
        if (expected_systime > systime) {
            systime = expected_systime;
        }

        /* Get RTP timestamp. */
        lldiv_t div = lldiv(systime, UCLOCK_FREQ);
        upipe_aes67_sink->timestamp = div.quot * 48000 + ((uint64_t)div.rem * 48000)/UCLOCK_FREQ;

        if (expected_timestamp != upipe_aes67_sink->timestamp)
            upipe_dbg_va(upipe, "timestamp mismatch expected %"PRIu32" got %"PRIu32,
                    expected_timestamp, upipe_aes67_sink->timestamp);

        /* Check size. */
        size_t samples = 0;
        uint8_t channels = 0;
        if (unlikely(!ubase_check(uref_sound_size(uref, &samples, &channels)))) {
            upipe_warn(upipe, "cannot read uref size");
            uref_free(uref);
            continue;
        }
        channels /= 4;

        int num_frames = (samples + upipe_aes67_sink->cached_samples) / upipe_aes67_sink->output_samples;

        /* Map uref. */
        const int32_t *src = NULL;
        if (unlikely(!ubase_check(uref_sound_read_int32_t(uref, 0, -1, &src, 1)))) {
            upipe_err(upipe, "unable to map uref");
            uref_free(uref);
            continue;
        }

        /* Add any cached samples. */
        samples += upipe_aes67_sink->cached_samples;
        /* Rewind source pointer for any cached samples. */
        src -= upipe_aes67_sink->cached_samples * channels;

        /* Make output packets. */
        uint8_t *data = upipe_aes67_sink->audio_data;
        for (int i = 0; i < num_frames; i++) {
            /* Pack audio data. */
            const int32_t *local_src = src + upipe_aes67_sink->output_samples * channels * i;

            if (upipe_aes67_sink->cached_samples) {
                for (int j = upipe_aes67_sink->cached_samples * channels; j < upipe_aes67_sink->output_samples * channels; j++) {
                    int32_t sample = local_src[j];
                    data[3*j+0] = (sample >> 24) & 0xff;
                    data[3*j+1] = (sample >> 16) & 0xff;
                    data[3*j+2] = (sample >>  8) & 0xff;
                }
                upipe_aes67_sink->cached_samples = 0;
            }

            else for (int j = 0; j < upipe_aes67_sink->output_samples * channels; j++) {
                int32_t sample = local_src[j];
                data[3*j+0] = (sample >> 24) & 0xff;
                data[3*j+1] = (sample >> 16) & 0xff;
                data[3*j+2] = (sample >>  8) & 0xff;
            }

            /* Sleep until packet is due. */
            uint64_t now = uclock_now(upipe_aes67_sink->uclock);
            if (now < systime) {
                struct timespec wait = { .tv_nsec = (systime - now) * 1000 / 27 };
                struct timespec left = { 0 };
                /* TODO: check remaining time. */
                if (unlikely(nanosleep(&wait, &left))) {
                    if (errno == EINTR)
                        upipe_warn_va(upipe, "nanosleep interrupted, left: %ld", left.tv_nsec);
                    else if (errno == EINVAL)
                        upipe_err(upipe, "invalid nanosleep");
                    else
                        upipe_err(upipe, "unknown return");
                }
            } else if (now > systime + (27000))
                upipe_warn_va(upipe,
                        "outputting late packet %"PRIu64" us, latency %"PRIu64" us",
                        (now - systime) / 27,
                        upipe_aes67_sink->latency / 27);

            /* Write packets. */
            int output_channels = upipe_aes67_sink->output_channels;
            int num_flows = 16 / output_channels;
            for (int flow = 0; flow < num_flows; flow++) {
                int channel_offset = flow * output_channels;

                /* TODO: Don't do a send() call for each flow. */
                upipe_aes67_sink_output(upipe, uref, NULL, flow, 0,
                        channel_offset, output_channels);
                if (upipe_aes67_sink->fd[1] != -1)
                    upipe_aes67_sink_output(upipe, uref, NULL, flow, 1,
                            channel_offset, output_channels);

                upipe_aes67_sink->mmap_frame_num = (upipe_aes67_sink->mmap_frame_num + 1) % MMAP_FRAME_NUM;
            }

            /* Adjust per packet values. */
            upipe_aes67_sink->seqnum += 1;
            upipe_aes67_sink->timestamp += upipe_aes67_sink->output_samples;
            systime += UCLOCK_FREQ * upipe_aes67_sink->output_samples / 48000;
        }
        /* Store the expected start of the next frame. */
        expected_systime = systime;
        expected_timestamp = upipe_aes67_sink->timestamp;

        if (samples % upipe_aes67_sink->output_samples) {
            /* Pack tail of uref into buffer. */
            const int32_t *local_src = src + upipe_aes67_sink->output_samples * channels * num_frames;
            for (int j = 0; j < (samples % upipe_aes67_sink->output_samples) * 16; j++) {
                int32_t sample = local_src[j];
                data[3*j+0] = (sample >> 24) & 0xff;
                data[3*j+1] = (sample >> 16) & 0xff;
                data[3*j+2] = (sample >>  8) & 0xff;
            }
            upipe_aes67_sink->cached_samples = samples % upipe_aes67_sink->output_samples;
        }

        uref_sound_unmap(uref, 0, -1, 1);
        uref_free(uref);
    }

    upipe_notice(upipe, "exiting run_thread");
    return NULL;
}

#define CHECK_RETURN(cmd) \
    do { \
        int ret = cmd; \
        if (ret) { \
            perror(#cmd); \
            return UBASE_ERR_EXTERNAL; \
        } \
    } while (0)

static int create_thread(struct upipe *upipe)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    pthread_attr_t attrs;
    CHECK_RETURN(pthread_attr_init(&attrs));
    CHECK_RETURN(pthread_attr_setschedpolicy(&attrs, SCHED_FIFO));
    CHECK_RETURN(pthread_attr_setinheritsched(&attrs, PTHREAD_EXPLICIT_SCHED));

    union { void *p; intptr_t i; } a;
    a.p = upipe_get_opaque(upipe, void*);
    cpu_set_t cpu;
    CPU_ZERO(&cpu);
    CPU_SET(a.i, &cpu);
    CHECK_RETURN(pthread_attr_setaffinity_np(&attrs, sizeof cpu, &cpu));

    struct sched_param params = {0};
    CHECK_RETURN(pthread_attr_getschedparam(&attrs, &params));
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    CHECK_RETURN(pthread_attr_setschedparam(&attrs, &params));

    int ret = pthread_create(&upipe_aes67_sink->pt, &attrs, run_thread, (void *)upipe);
    if (ret) {
        upipe_err_va(upipe, "pthread_create: %s", strerror(ret));
        return UBASE_ERR_ALLOC;
    }

    upipe_aes67_sink->thread_created = true;
    return UBASE_ERR_NONE;
}

#undef CHECK_RETURN

/** @internal @This allocates a aes67 sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_aes67_sink_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_aes67_sink_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);
    upipe_aes67_sink_init_urefcount(upipe);
    upipe_aes67_sink_init_uclock(upipe);

    upipe_aes67_sink->latency = 0;
    upipe_aes67_sink->fd[0] = upipe_aes67_sink->fd[1] = -1;

    upipe_aes67_sink->mmap[0] = upipe_aes67_sink->mmap[1] = MAP_FAILED;
    upipe_aes67_sink->mmap_frame_num = 0;

    upipe_aes67_sink->seqnum = 0;
    upipe_aes67_sink->cached_samples = 0;

    ulist_init(&upipe_aes67_sink->ulist);

    pthread_mutex_init(&upipe_aes67_sink->mutex, NULL);
    upipe_aes67_sink->thread_created = false;
    upipe_aes67_sink->stop = false;

    upipe_aes67_sink->output_samples = 6; /* TODO: other default to catch user not setting this? */
    upipe_aes67_sink->output_channels = (STEREO_FLOWS) ? 2 : 16;
    upipe_aes67_sink->mtu = MTU;

    upipe_aes67_sink->ifname[0] = upipe_aes67_sink->ifname[1] = NULL;
    memset(upipe_aes67_sink->sin, 0, sizeof upipe_aes67_sink->sin);
    memset(upipe_aes67_sink->sll, 0, sizeof upipe_aes67_sink->sll);
    memset(upipe_aes67_sink->flows, 0, sizeof upipe_aes67_sink->flows);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This outputs data to the aes67 sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_aes67_sink_output(struct upipe *upipe, struct uref *uref,
        struct upump **upump_p, int flow, int path, int channel_offset,
        int output_channels)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    for ( ; ; ) {
        int payload_len = upipe_aes67_sink->output_samples * output_channels * 3 + RTP_HEADER_SIZE;

            /* Get next frame to be used. */
            union frame_map frame = { .raw = upipe_aes67_sink->mmap[path] + upipe_aes67_sink->mmap_frame_num * MMAP_FRAME_SIZE };
            uint8_t *data = frame.v1->data;

            /* Fill in mmap stuff. */
            frame.v1->tph.tp_snaplen = frame.v1->tph.tp_len = RAW_HEADER_SIZE + payload_len;
            frame.v1->tph.tp_net = offsetof(struct v1, data);
            frame.v1->tph.tp_status = TP_STATUS_SEND_REQUEST;
            /* TODO: check for errors. */

            /* Fill in IP and UDP headers. */
            memcpy(data, upipe_aes67_sink->flows[flow][path].raw_header, RAW_HEADER_SIZE);
            udp_raw_set_len(data, payload_len);
            data += RAW_HEADER_SIZE;

            /* FIll in RTP headers. */
            memset(data, 0, RTP_HEADER_SIZE);
            rtp_set_hdr(data);
            rtp_set_type(data, 97);
            rtp_set_seqnum(data, upipe_aes67_sink->seqnum);
            rtp_set_timestamp(data, upipe_aes67_sink->timestamp);
            data += RTP_HEADER_SIZE;

        /* Slight optimization for single flow. */
        if (output_channels == 16)
        memcpy(data, upipe_aes67_sink->audio_data, upipe_aes67_sink->output_samples * 16 * 3);
        else for (int i = 0; i < upipe_aes67_sink->output_samples; i++) {
            int sample_size = 3 * output_channels;
            memcpy(data + i * sample_size,
                    upipe_aes67_sink->audio_data + 3*channel_offset + 3*16*i,
                    sample_size);
        }

        ssize_t ret = sendto(upipe_aes67_sink->fd[path], NULL, 0, 0,
                (struct sockaddr*)&upipe_aes67_sink->flows[flow][path].sll,
                sizeof upipe_aes67_sink->flows[flow][path].sll);
        if (unlikely(ret == -1)) {
            switch (errno) {
                case EINTR:
                    continue;
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    // FIXME
                    return false;
                case EBADF:
                case EFBIG:
                case EINVAL:
                case EIO:
                case ENOSPC:
                case EPIPE:
                default:
                    break;
            }
            /* Errors at this point come from ICMP messages such as
             * "port unreachable", and we do not want to kill the application
             * with transient errors. */
        }
        break;
    }

    return true;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_aes67_sink_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);
    uint64_t systime = 0;
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uint64_t latency = 0;
        uref_clock_get_latency(uref, &latency);
        if (latency > upipe_aes67_sink->latency)
            upipe_aes67_sink->latency = latency;
        uref_free(uref);
        return;
    }

    uref_clock_set_cr_dts_delay(uref, 0);
    uref_clock_set_dts_pts_delay(uref, 0);

    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
        upipe_warn(upipe, "received non-dated buffer");
    }

    pthread_mutex_lock(&upipe_aes67_sink->mutex);
    ulist_add(&upipe_aes67_sink->ulist, uref_to_uchain(uref));
    pthread_mutex_unlock(&upipe_aes67_sink->mutex);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static int upipe_aes67_sink_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))


    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_input(upipe, flow_def, NULL);

    if (!upipe_aes67_sink->thread_created)
        UBASE_RETURN(create_thread(upipe));

    return UBASE_ERR_NONE;
}

static int get_interface_details(struct upipe *upipe, const char *intf,
        struct sockaddr_in *sin, struct sockaddr_ll *sll)
{
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) < 0) {
        upipe_err_va(upipe, "getifaddrs: %m");
        return UBASE_ERR_ALLOC;
    }

    bool have_sin = false, have_sll = false;
    for (struct ifaddrs *ifap = ifa; ifap; ifap = ifap->ifa_next) {
        if (!strncmp(ifap->ifa_name, intf, IFNAMSIZ)) {
            if (ifap->ifa_addr->sa_family == AF_INET) {
                *sin = *(struct sockaddr_in *)ifap->ifa_addr;
                have_sin = true;
            }
            if (ifap->ifa_addr->sa_family == AF_PACKET) {
                *sll = *(struct sockaddr_ll *)ifap->ifa_addr;
                have_sll = true;
            }
        }
    }
    freeifaddrs(ifa);

    if (!have_sin) {
        upipe_err_va(upipe, "unable to get IP address for %s", intf);
        return UBASE_ERR_INVALID;
    }
    if (!have_sll) {
        upipe_err_va(upipe, "unable to get MAC address for %s", intf);
        return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

static int open_socket(struct upipe *upipe, const char *path_1, const char *path_2)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    if (path_1 == NULL)
        return UBASE_ERR_INVALID;

    /* Get interface index and IP address. */
    struct sockaddr_in sin;
    struct sockaddr_ll sll;
    UBASE_RETURN(get_interface_details(upipe, path_1, &sin, &sll));

    upipe_dbg_va(upipe, "opening socket for %s %s", path_1, inet_ntoa(sin.sin_addr));
    /* Open socket in the desired mode. */
    int fd = socket(AF_PACKET, SOCK_DGRAM, 0);
    if (fd < 0) {
        upipe_err_va(upipe, "unable to open socket (%m)");
        return UBASE_ERR_EXTERNAL;
    }

    /* Set socket options. */
    int val;

    /* Drop/discard/ignore malformed packets. */
    val = 1;
    if (setsockopt(fd, SOL_PACKET, PACKET_LOSS, &val, sizeof val)) {
        upipe_err_va(upipe, "unable to set PACKET_LOSS (%m)");
    }

    /* Default but try anyway. */
    val = TPACKET_V1;
    if (setsockopt(fd, SOL_PACKET, PACKET_VERSION, &val, sizeof val)) {
        upipe_err_va(upipe, "unable to set PACKET_VERSION (%m)");
    }

    /* Setup TX ring for mmap. */
    struct tpacket_req req = {
        .tp_block_size = MMAP_BLOCK_SIZE, // getpagesize()
        .tp_block_nr   = MMAP_BLOCK_NUM,
        .tp_frame_size = MMAP_FRAME_SIZE, // TPACKET_ALIGNMENT
        .tp_frame_nr   = MMAP_FRAME_NUM,
    };
    upipe_dbg_va(upipe, "tp_block_size: %u, tp_block_nr %u, tp_frame_size: %u, tp_frame_nr: %u",
            req.tp_block_size, req.tp_block_nr, req.tp_frame_size, req.tp_frame_nr);
    upipe_dbg_va(upipe, "tp_block_size %% getpagesize(): %ld, tp_frame_size %% TPACKET_ALIGNMENT: %ld",
            MMAP_BLOCK_SIZE % getpagesize(), MMAP_FRAME_SIZE % TPACKET_ALIGNMENT);
    if (setsockopt(fd, SOL_PACKET, PACKET_TX_RING, (void *)&req, sizeof req)) {
        upipe_err_va(upipe, "unable to set PACKET_TX_RING (%m)");
        close(fd);
        return UBASE_ERR_EXTERNAL;
    }

    /* Bind to the given interface. */
    if (bind(fd, (struct sockaddr *)&sll, sizeof sll) < 0) {
        upipe_err_va(upipe, "unable to bind: %m");
        close(fd);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_aes67_sink->mmap[0] = mmap(0, MMAP_BLOCK_SIZE * MMAP_BLOCK_NUM,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (upipe_aes67_sink->mmap[0] == MAP_FAILED) {
        upipe_err_va(upipe, "unable to mmap (%m)");
        close(fd);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_aes67_sink->fd[0] = fd;
    upipe_aes67_sink->sin[0] = sin;
    upipe_aes67_sink->sll[0] = sll;
    upipe_aes67_sink->ifname[0] = strdup(path_1);

    /* Query for mtu. */
    int mtu[2] = { INT_MAX, INT_MAX };
    struct ifreq r;
    strncpy(r.ifr_name, upipe_aes67_sink->ifname[0], sizeof r.ifr_name);
    if (ioctl(upipe_aes67_sink->fd[0], SIOCGIFMTU, &r) == -1) {
        upipe_err_va(upipe, "SIOCGIFMTU: %m");
    } else {
        upipe_dbg_va(upipe, "%s mtu: %d", upipe_aes67_sink->ifname[0], r.ifr_mtu);
        mtu[0] = r.ifr_mtu;
    }

    /* Handle second path. */
    if (path_2 && strlen(path_2)) {
        UBASE_RETURN(get_interface_details(upipe, path_2, &sin, &sll));

        upipe_dbg_va(upipe, "opening socket for %s %s", path_2, inet_ntoa(sin.sin_addr));
        fd = socket(AF_PACKET, SOCK_DGRAM, 0);
        if (fd < 0) {
            upipe_err_va(upipe, "unable to open socket (%m)");
            return UBASE_ERR_EXTERNAL;
        }

        val = 1;
        if (setsockopt(fd, SOL_PACKET, PACKET_LOSS, &val, sizeof val)) {
            upipe_err_va(upipe, "unable to set PACKET_LOSS (%m)");
        }
        val = TPACKET_V1;
        if (setsockopt(fd, SOL_PACKET, PACKET_VERSION, &val, sizeof val)) {
            upipe_err_va(upipe, "unable to set PACKET_VERSION (%m)");
        }
        if (setsockopt(fd, SOL_PACKET, PACKET_TX_RING, (void *)&req, sizeof req)) {
            upipe_err_va(upipe, "unable to set PACKET_TX_RING (%m)");
            close(fd);
            return UBASE_ERR_EXTERNAL;
        }

        if (bind(fd, (struct sockaddr *)&sll, sizeof sll) < 0) {
            upipe_err_va(upipe, "unable to bind: %m");
            close(fd);
            return UBASE_ERR_EXTERNAL;
        }

        upipe_aes67_sink->mmap[1] = mmap(0, MMAP_BLOCK_SIZE * MMAP_BLOCK_NUM,
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (upipe_aes67_sink->mmap[1] == MAP_FAILED) {
            upipe_err_va(upipe, "unable to mmap (%m)");
            close(fd);
            return UBASE_ERR_EXTERNAL;
        }

        upipe_aes67_sink->fd[1] = fd;
        upipe_aes67_sink->sin[1] = sin;
        upipe_aes67_sink->sll[1] = sll;
        upipe_aes67_sink->ifname[1] = strdup(path_2);

        strncpy(r.ifr_name, upipe_aes67_sink->ifname[1], sizeof r.ifr_name);
        if (ioctl(upipe_aes67_sink->fd[1], SIOCGIFMTU, &r) == -1) {
            upipe_err_va(upipe, "SIOCGIFMTU: %m");
        } else {
            upipe_dbg_va(upipe, "%s mtu: %d", upipe_aes67_sink->ifname[1], r.ifr_mtu);
            mtu[1] = r.ifr_mtu;
        }
    }

    /* Copy lowest mtu if they were obtained at all. */
    if (mtu[0] != INT_MAX && mtu[0] <= mtu[1])
        upipe_aes67_sink->mtu = mtu[0];
    if (mtu[1] != INT_MAX && mtu[1] <= mtu[0])
        upipe_aes67_sink->mtu = mtu[1];

    return UBASE_ERR_NONE;
}

static int set_flow_destination(struct upipe * upipe, int flow,
        const char *path_1, const char *path_2)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    /* Check arguments are okay. */
    if (unlikely(path_1 == NULL))
        return UBASE_ERR_INVALID;
    if (unlikely((path_2 == NULL || strlen(path_2) == 0) && upipe_aes67_sink->fd[1] != -1))
        return UBASE_ERR_INVALID;
    if (unlikely(flow < 0 || flow >= AES67_MAX_FLOWS)) {
        upipe_err_va(upipe, "flow %d is not in the range 0..%d", flow, AES67_MAX_FLOWS-1);
        return UBASE_ERR_INVALID;
    }

    struct aes67_flow *aes67_flow = upipe_aes67_sink->flows[flow];

    /* Parse first path. */
    char *path = strdup(path_1);
    UBASE_ALLOC_RETURN(path);
    if (!upipe_udp_parse_node_service(upipe, path, NULL, 0, NULL,
                (struct sockaddr_storage *)&aes67_flow[0].sin)) {
        free(path);
        return UBASE_ERR_INVALID;
    }
    free(path);
    upipe_dbg_va(upipe, "flow %d path 0 destination set to %s:%u", flow,
            inet_ntoa(aes67_flow[0].sin.sin_addr),
            ntohs(aes67_flow[0].sin.sin_port));

    /* Write IP and UDP headers. */
    upipe_udp_raw_fill_headers(NULL, aes67_flow[0].raw_header,
            upipe_aes67_sink->sin[0].sin_addr.s_addr, aes67_flow[0].sin.sin_addr.s_addr,
            ntohs(aes67_flow[0].sin.sin_port), ntohs(aes67_flow[0].sin.sin_port),
            10, 0, upipe_aes67_sink->output_samples * upipe_aes67_sink->output_channels * 3 + RTP_HEADER_SIZE);

    /* Set ethernet details and the inferface index. */
    aes67_flow[0].sll = (struct sockaddr_ll) {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETH_P_IP),
        .sll_ifindex = upipe_aes67_sink->sll[0].sll_ifindex,
        .sll_halen = ETH_ALEN,
    };

    /* Set MAC address. */
    uint32_t dst_ip = ntohl(aes67_flow[0].sin.sin_addr.s_addr);

    /* If a multicast IP address, fill a multicast MAC address. */
    if (IN_MULTICAST(dst_ip)) {
        aes67_flow[0].sll.sll_addr[0] = 0x01;
        aes67_flow[0].sll.sll_addr[1] = 0x00;
        aes67_flow[0].sll.sll_addr[2] = 0x5e;
        aes67_flow[0].sll.sll_addr[3] = (dst_ip >> 16) & 0x7f;
        aes67_flow[0].sll.sll_addr[4] = (dst_ip >>  8) & 0xff;
        aes67_flow[0].sll.sll_addr[5] = (dst_ip      ) & 0xff;
    }

    /* Otherwise query ARP for the destination address. */
    else {
        struct arpreq arp = { .arp_flags = 0 };
        /* Copy dest IP address to the struct. */
        memcpy(&arp.arp_pa, &aes67_flow[0].sin, sizeof aes67_flow[0].sin);
        /* Copy interface name to the struct. */
        strncpy(arp.arp_dev, upipe_aes67_sink->ifname[0], sizeof arp.arp_dev);

        /* Upon success copy the MAC address to the destination struct. */
        if (ioctl(upipe_aes67_sink->fd[0], SIOCGARP, &arp) != -1) {
            memcpy(aes67_flow[0].sll.sll_addr, arp.arp_ha.sa_data, ETH_ALEN);
        }

        /* Otherwise report an error and fill with broadcast. */
        else {
            upipe_err_va(upipe, "unable to get MAC address for %s: (%d) %m",
                    inet_ntoa(aes67_flow[0].sin.sin_addr), errno);
            aes67_flow[0].sll.sll_addr[0] = 0xff;
            aes67_flow[0].sll.sll_addr[1] = 0xff;
            aes67_flow[0].sll.sll_addr[2] = 0xff;
            aes67_flow[0].sll.sll_addr[3] = 0xff;
            aes67_flow[0].sll.sll_addr[4] = 0xff;
            aes67_flow[0].sll.sll_addr[5] = 0xff;
        }
    }

    if (path_2 && strlen(path_2)) {
        path = strdup(path_2);
        UBASE_ALLOC_RETURN(path);
        if (!upipe_udp_parse_node_service(upipe, path, NULL, 0, NULL,
                    (struct sockaddr_storage *)&aes67_flow[1].sin)) {
            free(path);
            return UBASE_ERR_INVALID;
        }
        free(path);
        upipe_dbg_va(upipe, "flow %d path 1 destination set to %s:%u", flow,
                inet_ntoa(aes67_flow[1].sin.sin_addr),
                ntohs(aes67_flow[1].sin.sin_port));

        upipe_udp_raw_fill_headers(NULL, aes67_flow[1].raw_header,
                upipe_aes67_sink->sin[1].sin_addr.s_addr, aes67_flow[1].sin.sin_addr.s_addr,
                ntohs(aes67_flow[1].sin.sin_port), ntohs(aes67_flow[1].sin.sin_port),
                10, 0, upipe_aes67_sink->output_samples * upipe_aes67_sink->output_channels * 3 + RTP_HEADER_SIZE);

        aes67_flow[1].sll = (struct sockaddr_ll) {
            .sll_family = AF_PACKET,
                .sll_protocol = htons(ETH_P_IP),
                .sll_ifindex = upipe_aes67_sink->sll[1].sll_ifindex,
                .sll_halen = ETH_ALEN,
        };

        dst_ip = ntohl(aes67_flow[1].sin.sin_addr.s_addr);
        if (IN_MULTICAST(dst_ip)) {
            aes67_flow[1].sll.sll_addr[0] = 0x01;
            aes67_flow[1].sll.sll_addr[1] = 0x00;
            aes67_flow[1].sll.sll_addr[2] = 0x5e;
            aes67_flow[1].sll.sll_addr[3] = (dst_ip >> 16) & 0x7f;
            aes67_flow[1].sll.sll_addr[4] = (dst_ip >>  8) & 0xff;
            aes67_flow[1].sll.sll_addr[5] = (dst_ip      ) & 0xff;
        } else {
            struct arpreq arp = { .arp_flags = 0 };
            memcpy(&arp.arp_pa, &aes67_flow[1].sin, sizeof aes67_flow[1].sin);
            strncpy(arp.arp_dev, upipe_aes67_sink->ifname[1], sizeof arp.arp_dev);

            if (ioctl(upipe_aes67_sink->fd[1], SIOCGARP, &arp) != -1) {
                memcpy(aes67_flow[1].sll.sll_addr, arp.arp_ha.sa_data, ETH_ALEN);
            } else {
                upipe_err_va(upipe, "unable to get MAC address for %s: (%d) %m",
                        inet_ntoa(aes67_flow[1].sin.sin_addr), errno);
                aes67_flow[1].sll.sll_addr[0] = 0xff;
                aes67_flow[1].sll.sll_addr[1] = 0xff;
                aes67_flow[1].sll.sll_addr[2] = 0xff;
                aes67_flow[1].sll.sll_addr[3] = 0xff;
                aes67_flow[1].sll.sll_addr[4] = 0xff;
                aes67_flow[1].sll.sll_addr[5] = 0xff;
            }
        }
    }

    return UBASE_ERR_NONE;
}

static int upipe_aes67_sink_set_option(struct upipe *upipe, const char *option,
        const char *value)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    if (!option || !value)
        return UBASE_ERR_INVALID;

    if (!strcmp(option, "output-samples")) {
        upipe_aes67_sink->output_samples = atoi(value);
        if (upipe_aes67_sink->output_samples < 0 || upipe_aes67_sink->output_samples > MAX_SAMPLES_PER_PACKET) {
            upipe_err_va(upipe, "output-samples (%d) not in range 0..%d",
                    upipe_aes67_sink->output_samples, MAX_SAMPLES_PER_PACKET);
            return UBASE_ERR_INVALID;
        }

        /* A sample packs to 3 bytes.  16 channels. */
        int needed_size = TRANSMISSION_UNIT_SIZE(upipe_aes67_sink->output_samples * upipe_aes67_sink->output_channels * 3);
        if (needed_size > upipe_aes67_sink->mtu) {
            upipe_err_va(upipe, "requested frame or packet size (%d bytes, %d samples) is greater than MTU (%d)",
                    needed_size, upipe_aes67_sink->output_samples, upipe_aes67_sink->mtu);
            return UBASE_ERR_INVALID;
        }

        return UBASE_ERR_NONE;
    }

    upipe_err_va(upipe, "Unknown option %s", option);
    return UBASE_ERR_INVALID;
}


/** @internal @This processes control commands on an aes67 sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_aes67_sink_control(struct upipe *upipe,
                                  int command, va_list args)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);

        case UPIPE_ATTACH_UCLOCK:
            upipe_aes67_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_aes67_sink_set_flow_def(upipe, flow_def);
        }

        case UPIPE_AES67_SINK_OPEN_SOCKET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AES67_SINK_SIGNATURE)
            const char *path_1 = va_arg(args, const char *);
            const char *path_2 = va_arg(args, const char *);
            return open_socket(upipe, path_1, path_2);
        }

        case UPIPE_AES67_SINK_SET_FLOW_DESTINATION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AES67_SINK_SIGNATURE)
            int flow = va_arg(args, int);
            const char *path_1 = va_arg(args, const char *);
            const char *path_2 = va_arg(args, const char *);
            return set_flow_destination(upipe, flow, path_1, path_2);
        }

        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *value  = va_arg(args, const char *);
            return upipe_aes67_sink_set_option(upipe, option, value);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_aes67_sink_free(struct upipe *upipe)
{
    struct upipe_aes67_sink *upipe_aes67_sink = upipe_aes67_sink_from_upipe(upipe);

    /* Stop thread. */
    pthread_mutex_lock(&upipe_aes67_sink->mutex);
    upipe_aes67_sink->stop = true;
    pthread_mutex_unlock(&upipe_aes67_sink->mutex);

    /* Wait for thread to exit. */
    if (upipe_aes67_sink->thread_created)
        pthread_join(upipe_aes67_sink->pt, NULL);
    /* Clean up mutex. */
    pthread_mutex_destroy(&upipe_aes67_sink->mutex); /* Check return value? */

    if (likely(upipe_aes67_sink->fd[0] != -1)) {
        close(upipe_aes67_sink->fd[0]);
        if (upipe_aes67_sink->fd[1] != -1)
            close(upipe_aes67_sink->fd[1]);
    }

    if (upipe_aes67_sink->ifname[0])
        free(upipe_aes67_sink->ifname[0]);
    if (upipe_aes67_sink->ifname[1])
        free(upipe_aes67_sink->ifname[1]);

    upipe_throw_dead(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_aes67_sink->ulist, uchain, uchain_tmp) {
        ulist_delete(uchain);
        uref_free(uref_from_uchain(uchain));
    }

    upipe_aes67_sink_clean_uclock(upipe);
    upipe_aes67_sink_clean_urefcount(upipe);
    upipe_aes67_sink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_aes67_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_AES67_SINK_SIGNATURE,

    .upipe_alloc = upipe_aes67_sink_alloc,
    .upipe_input = upipe_aes67_sink_input,
    .upipe_control = upipe_aes67_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all aes67 sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_aes67_sink_mgr_alloc(void)
{
    return &upipe_aes67_sink_mgr;
}

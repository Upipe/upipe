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
 * @short Upipe sink module for udp
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_udp_sink_fast.h>
#include "upipe_udp.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
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

/** tolerance for late packets */
#define SYSTIME_TOLERANCE UCLOCK_FREQ
/** print late packets */
#define SYSTIME_PRINT (UCLOCK_FREQ / 100)
/** expected flow definition on all flows */
#define EXPECTED_FLOW_DEF    "block."

#define UDP_DEFAULT_TTL 0
#define UDP_DEFAULT_PORT 1234

/** @hidden */
static bool upipe_udpsink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p, int which_fd);

/** @internal @This is the private context of a udp sink pipe. */
struct upipe_udpsink {
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
    /** socket uri */
    char *uri;
    /** interface indicies. */
    int ifindex[2];

    void *mmap[2];
    size_t mmap_size[2];
    int frame_num;
    struct sockaddr_ll peer_addr[2];

    struct uchain ulist;

    bool thread_created;
    pthread_t pt;
    pthread_mutex_t mutex;
    uatomic_uint32_t stop;

    /** RAW sockets */
    bool raw;
    /** RAW header */
    uint8_t raw_header[2][RAW_HEADER_SIZE];

    /** destination for not-connected socket */
    struct sockaddr_storage addr;
    /** destination for not-connected socket (size) */
    socklen_t addrlen;

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

UPIPE_HELPER_UPIPE(upipe_udpsink, upipe, UPIPE_UDPSINK_FAST_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_udpsink, urefcount, upipe_udpsink_free)
UPIPE_HELPER_VOID(upipe_udpsink)
UPIPE_HELPER_UCLOCK(upipe_udpsink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

static void *run_thread(void *upipe_pointer)
{
    struct upipe *upipe = upipe_pointer;
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    struct uref *uref = NULL;
    struct uchain *uchain = NULL;

    /* Run until told to stop. */
    while (uatomic_load(&upipe_udpsink->stop) == 0) {
        pthread_mutex_lock(&upipe_udpsink->mutex);
        uchain = ulist_pop(&upipe_udpsink->ulist);
        pthread_mutex_unlock(&upipe_udpsink->mutex);

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

        upipe_udpsink_output(upipe, uref, NULL, 0);
        upipe_udpsink_output(upipe, uref, NULL, 1);
        /* TODO: what to do if the output doesn't use the uref? */

        size_t num_frames = 0, payload_len = 0;
        if (likely(ubase_check(uref_block_size(uref, &payload_len)))) {
            num_frames = payload_len / 288;
            if (payload_len % 288)
                upipe_warn(upipe, "not whole uref consumed");
            num_frames = payload_len / 288;
        }
        upipe_udpsink->frame_num = (upipe_udpsink->frame_num + num_frames) % MMAP_FRAME_NUM;
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
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);

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

    int ret = pthread_create(&upipe_udpsink->pt, &attrs, run_thread, (void *)upipe);
    if (ret) {
        upipe_err_va(upipe, "pthread_create: %s", strerror(ret));
        return UBASE_ERR_ALLOC;
    }

    upipe_udpsink->thread_created = true;
    return UBASE_ERR_NONE;
}

#undef CHECK_RETURN

/** @internal @This allocates a udp sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_udpsink_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_udpsink_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    upipe_udpsink_init_urefcount(upipe);
    upipe_udpsink_init_uclock(upipe);
    upipe_udpsink->latency = 0;
    upipe_udpsink->fd[0] = upipe_udpsink->fd[1] = -1;
    upipe_udpsink->uri = NULL;
    upipe_udpsink->raw = false;
    upipe_udpsink->addrlen = 0;
    upipe_udpsink->thread_created = false;
    pthread_mutex_init(&upipe_udpsink->mutex, NULL);

    upipe_udpsink->mmap[0] = upipe_udpsink->mmap[1] = MAP_FAILED;
    upipe_udpsink->mmap_size[0] = upipe_udpsink->mmap_size[1] = 0;
    upipe_udpsink->frame_num = 0;
    upipe_udpsink->peer_addr[0] = upipe_udpsink->peer_addr[1] =
        (struct sockaddr_ll) {
            .sll_family = AF_PACKET,
            .sll_protocol = htons(ETH_P_IP),
            .sll_halen = ETH_ALEN,
            .sll_addr = {0xff,0xff,0xff,0xff,0xff,0xff}, /* TODO: get right one? */
        };


    ulist_init(&upipe_udpsink->ulist);
    uatomic_init(&upipe_udpsink->stop, 0);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This outputs data to the udp sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_udpsink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p, int which_fd)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    int slept = 0;

    if (unlikely(upipe_udpsink->fd[which_fd] == -1)) {
        upipe_warn(upipe, "received a buffer before opening a socket");
        return true;
    }

    if (likely(upipe_udpsink->uclock == NULL))
        goto write_buffer;

    uint64_t now = uclock_now(upipe_udpsink->uclock);
    uint64_t systime = 0;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
        upipe_warn(upipe, "received non-dated buffer");
        goto write_buffer;
    }

    if (now < systime) {
#if 0
        useconds_t wait = (systime - now) / 27;
        usleep(wait);
#else
        struct timespec wait = { .tv_nsec = (systime - now) * 1000 / 27 };
        struct timespec left = { 0 };
        slept = wait.tv_nsec / 1000;
        /* TODO: check return value and remaining time. */
        if (unlikely(nanosleep(&wait, &left))) {
            if (errno == EINTR)
                upipe_warn_va(upipe, "nanosleep interrupted, left: %ld", left.tv_nsec);
            else if (errno == EINVAL)
                upipe_err(upipe, "invalid nanosleep");
            else
                upipe_err(upipe, "unknown return");
        } else {
            //upipe_dbg_va(upipe, "waited %ld ns", wait.tv_nsec);
        }
#endif
    } else if (now > systime + (27000))
        upipe_warn_va(upipe,
                      "outputting late packet %"PRIu64" us, latency %"PRIu64" us slept %u us",
                      (now - systime) / 27,
                      upipe_udpsink->latency / 27, slept);

write_buffer:
    for ( ; ; ) {
        size_t payload_len = 0;
        if (unlikely(!ubase_check(uref_block_size(uref, &payload_len)))) {
            upipe_warn(upipe, "cannot read ubuf size");
            return false;
        }

        int num_frames = payload_len / 288;
        if (num_frames > MMAP_FRAME_NUM) {
            upipe_err(upipe, "uref too big");
            return false;
        }

        /* Populate the frames. */
        for (int i = 0; i < num_frames; i++) {
            int mmap_frame = (i + upipe_udpsink->frame_num) % MMAP_FRAME_NUM;
            /* Get next frame to be used. */
            union frame_map frame = { .raw = upipe_udpsink->mmap[which_fd] + mmap_frame * MMAP_FRAME_SIZE };

            /* Fill in mmap stuff. */
            frame.v1->tph.tp_snaplen = frame.v1->tph.tp_len = RAW_HEADER_SIZE + 288;
            frame.v1->tph.tp_net = offsetof(struct v1, data);
            frame.v1->tph.tp_status = TP_STATUS_SEND_REQUEST;
            /* TODO: check for errors. */

            /* Fill in IP and UDP headers. */
            memcpy(frame.v1->data, upipe_udpsink->raw_header[which_fd], RAW_HEADER_SIZE);
            udp_raw_set_len(frame.v1->data, 288);

            /* TODO: RTP headers. */

            int err = uref_block_extract(uref, 288*i, 288,
                    frame.v1->data + RAW_HEADER_SIZE);
            if (!ubase_check(err)) {
                upipe_throw_error(upipe, err);
                return false;
            }
        }

        ssize_t ret = sendto(upipe_udpsink->fd[which_fd], NULL, 0, 0,
                (struct sockaddr*)&upipe_udpsink->peer_addr[which_fd], sizeof upipe_udpsink->peer_addr[which_fd]);
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
static void upipe_udpsink_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    uint64_t systime = 0;
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uint64_t latency = 0;
        uref_clock_get_latency(uref, &latency);
        if (latency > upipe_udpsink->latency)
            upipe_udpsink->latency = latency;
        uref_free(uref);
        return;
    }

    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
        upipe_warn(upipe, "received non-dated buffer");
    }

    pthread_mutex_lock(&upipe_udpsink->mutex);
    ulist_add(&upipe_udpsink->ulist, uref_to_uchain(uref));
    pthread_mutex_unlock(&upipe_udpsink->mutex);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return false if the flow definition is not handled
 */
static int upipe_udpsink_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_input(upipe, flow_def, NULL);

    if (!upipe_udpsink->thread_created)
        UBASE_RETURN(create_thread(upipe));

    return UBASE_ERR_NONE;
}

/** @internal @This returns the uri of the currently opened socket.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the socket
 * @return an error code
 */
static int _upipe_udpsink_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_udpsink->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the socket
 * @param mode mode of opening the socket
 * @return an error code
 */
static int _upipe_udpsink_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    bool use_tcp = false;

    if (unlikely(upipe_udpsink->fd[0] != -1)) {
        if (likely(upipe_udpsink->uri != NULL))
            upipe_notice_va(upipe, "closing socket %s", upipe_udpsink->uri);
        close(upipe_udpsink->fd[0]);
        if (upipe_udpsink->fd[1] != -1)
            close(upipe_udpsink->fd[1]);
    }
    ubase_clean_str(&upipe_udpsink->uri);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    char *uri_a, *uri_b;
    upipe_udpsink->uri = uri_a = strdup(uri);
    UBASE_ALLOC_RETURN(uri_a);

    uri_b = strchr(uri_a, '+');
    if (uri_b)
        *uri_b++ = '\0'; /* Remove + character and start 2nd uri after it. */

    /* Open 1st socket. */
    upipe_udpsink->fd[0] = upipe_udp_open_socket(upipe, uri_a,
            UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp,
            &upipe_udpsink->raw, upipe_udpsink->raw_header[0],
            &upipe_udpsink->ifindex[0]);
    if (unlikely(upipe_udpsink->fd[0] == -1)) {
        upipe_err_va(upipe, "can't open uri %s", uri_a);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_udpsink->mmap_size[0] = MMAP_BLOCK_SIZE * MMAP_BLOCK_NUM;
    upipe_udpsink->mmap[0] = mmap(0, upipe_udpsink->mmap_size[0], PROT_READ | PROT_WRITE,
            MAP_SHARED, upipe_udpsink->fd[0], 0);
    if (upipe_udpsink->mmap[0] == MAP_FAILED) {
        upipe_err_va(upipe, "unable to mmap: %m");
        return UBASE_ERR_EXTERNAL;
    }
    upipe_udpsink->peer_addr[0].sll_ifindex = upipe_udpsink->ifindex[0];

    /* Open 2nd socket. */
    if (uri_b) {
        uri_b[-1] = '+'; /* Restore + character. */
        upipe_udpsink->fd[1] = upipe_udp_open_socket(upipe, uri_b,
                UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp,
                &upipe_udpsink->raw, upipe_udpsink->raw_header[1],
                &upipe_udpsink->ifindex[1]);
        if (unlikely(upipe_udpsink->fd[1] == -1)) {
            upipe_err_va(upipe, "can't open uri %s", uri_b);
            ubase_clean_fd(&upipe_udpsink->fd[0]);
            return UBASE_ERR_EXTERNAL;
        }

        upipe_udpsink->mmap_size[1] = MMAP_BLOCK_SIZE * MMAP_BLOCK_NUM;
        upipe_udpsink->mmap[1] = mmap(0, upipe_udpsink->mmap_size[1], PROT_READ | PROT_WRITE,
                MAP_SHARED, upipe_udpsink->fd[1], 0);
        if (upipe_udpsink->mmap[1] == MAP_FAILED) {
            upipe_err_va(upipe, "unable to mmap: %m");
            return UBASE_ERR_EXTERNAL;
        }
        upipe_udpsink->peer_addr[1].sll_ifindex = upipe_udpsink->ifindex[1];
    }

    upipe_notice_va(upipe, "opening uri %s", upipe_udpsink->uri);
    return UBASE_ERR_NONE;
}

/** @internal @This flushes all currently held buffers, and unblocks the
 * sources.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_udpsink_flush(struct upipe *upipe)
{
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a udp sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_udpsink_control(struct upipe *upipe,
                                  int command, va_list args)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);

        case UPIPE_ATTACH_UCLOCK:
            upipe_udpsink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_udpsink_set_flow_def(upipe, flow_def);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return _upipe_udpsink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return _upipe_udpsink_set_uri(upipe, uri);
        }

        case UPIPE_UDPSINK_FAST_GET_FD: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSINK_FAST_SIGNATURE)
            int *fd = va_arg(args, int *);
            *fd = upipe_udpsink->fd[0];
            return UBASE_ERR_NONE;
        }
        case UPIPE_UDPSINK_FAST_SET_FD: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSINK_FAST_SIGNATURE)
            upipe_udpsink->fd[0] = va_arg(args, int );
            return UBASE_ERR_NONE;
        }
        case UPIPE_UDPSINK_FAST_SET_PEER: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSINK_FAST_SIGNATURE)
            const struct sockaddr *s = va_arg(args, const struct sockaddr *);
            upipe_udpsink->addrlen = va_arg(args, socklen_t);
            memcpy(&upipe_udpsink->addr, s, upipe_udpsink->addrlen);
            return UBASE_ERR_NONE;
        }
        case UPIPE_FLUSH:
            return upipe_udpsink_flush(upipe);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsink_free(struct upipe *upipe)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);

    /* Stop thread. */
    uatomic_store(&upipe_udpsink->stop, 1);
    /* Wait for thread to exit. */
    pthread_join(upipe_udpsink->pt, NULL);
    /* Clean up mutex. */
    pthread_mutex_destroy(&upipe_udpsink->mutex); /* Check return value? */

    if (likely(upipe_udpsink->fd[0] != -1)) {
        if (likely(upipe_udpsink->uri != NULL))
            upipe_notice_va(upipe, "closing socket %s", upipe_udpsink->uri);
        close(upipe_udpsink->fd[0]);
        if (upipe_udpsink->fd[1] != -1)
            close(upipe_udpsink->fd[1]);
    }

    upipe_throw_dead(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_udpsink->ulist, uchain, uchain_tmp) {
        ulist_delete(uchain);
        uref_free(uref_from_uchain(uchain));
    }

    free(upipe_udpsink->uri);
    upipe_udpsink_clean_uclock(upipe);
    upipe_udpsink_clean_urefcount(upipe);
    upipe_udpsink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_udpsink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_UDPSINK_FAST_SIGNATURE,

    .upipe_alloc = upipe_udpsink_alloc,
    .upipe_input = upipe_udpsink_input,
    .upipe_control = upipe_udpsink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all udp sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsink_fast_mgr_alloc(void)
{
    return &upipe_udpsink_mgr;
}

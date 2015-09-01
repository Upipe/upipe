/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-modules/upipe_udp_sink.h>
#include "upipe_udp.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>

/** tolerance for late packets */
#define SYSTIME_TOLERANCE UCLOCK_FREQ
/** print late packets */
#define SYSTIME_PRINT (UCLOCK_FREQ / 100)
/** expected flow definition on all flows */
#define EXPECTED_FLOW_DEF    "block."

#define UDP_DEFAULT_TTL 0
#define UDP_DEFAULT_PORT 1234

/** @hidden */
static void upipe_udpsink_watcher(struct upump *upump);
/** @hidden */
static bool upipe_udpsink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p);

/** @internal @This is the private context of a udp sink pipe. */
struct upipe_udpsink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** write watcher */
    struct upump *upump;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** delay applied to systime attribute when uclock is provided */
    uint64_t latency;
    /** file descriptor */
    int fd;
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

    /** RAW sockets */
    bool raw;
    /** RAW header */
    uint8_t raw_header[RAW_HEADER_SIZE];

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_udpsink, upipe, UPIPE_UDPSINK_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_udpsink, urefcount, upipe_udpsink_free)
UPIPE_HELPER_VOID(upipe_udpsink)
UPIPE_HELPER_UPUMP_MGR(upipe_udpsink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_udpsink, upump, upump_mgr)
UPIPE_HELPER_INPUT(upipe_udpsink, urefs, nb_urefs, max_urefs, blockers, upipe_udpsink_output)
UPIPE_HELPER_UCLOCK(upipe_udpsink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

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
    upipe_udpsink_init_upump_mgr(upipe);
    upipe_udpsink_init_upump(upipe);
    upipe_udpsink_init_input(upipe);
    upipe_udpsink_init_uclock(upipe);
    upipe_udpsink->latency = 0;
    upipe_udpsink->fd = -1;
    upipe_udpsink->uri = NULL;
    upipe_udpsink->raw = false;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This starts the watcher waiting for the sink to unblock.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsink_poll(struct upipe *upipe)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    if (unlikely(!ubase_check(upipe_udpsink_check_upump_mgr(upipe)))) {
        upipe_err_va(upipe, "can't get upump_mgr");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }
    struct upump *watcher = upump_alloc_fd_write(upipe_udpsink->upump_mgr,
                                                 upipe_udpsink_watcher, upipe,
                                                 upipe_udpsink->fd);
    if (unlikely(watcher == NULL)) {
        upipe_err_va(upipe, "can't create watcher");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
    } else {
        upipe_udpsink_set_upump(upipe, watcher);
        upump_start(watcher);
    }
}

/** @internal @This outputs data to the udp sink.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return true if the uref was processed
 */
static bool upipe_udpsink_output(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        uint64_t latency = 0;
        uref_clock_get_latency(uref, &latency);
        if (latency > upipe_udpsink->latency)
            upipe_udpsink->latency = latency;
        uref_free(uref);
        return true;
    }

    if (unlikely(upipe_udpsink->fd == -1)) {
        uref_free(uref);
        upipe_warn(upipe, "received a buffer before opening a socket");
        return true;
    }

    if (likely(upipe_udpsink->uclock == NULL))
        goto write_buffer;

    uint64_t systime = 0;
    if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &systime)))) {
        upipe_warn(upipe, "received non-dated buffer");
        goto write_buffer;
    }

    uint64_t now = uclock_now(upipe_udpsink->uclock);
    systime += upipe_udpsink->latency;
    if (unlikely(now < systime)) {
        upipe_udpsink_check_upump_mgr(upipe);
        if (likely(upipe_udpsink->upump_mgr != NULL)) {
            upipe_verbose_va(upipe, "sleeping %"PRIu64" (%"PRIu64")",
                             systime - now, systime);
            upipe_udpsink_wait_upump(upipe, systime - now,
                                     upipe_udpsink_watcher);
            return false;
        }
    } else if (now > systime + SYSTIME_TOLERANCE) {
        upipe_warn_va(upipe,
                      "dropping late packet %"PRIu64" ms, latency %"PRIu64" ms",
                      (now - systime) / (UCLOCK_FREQ / 1000),
                      upipe_udpsink->latency / (UCLOCK_FREQ / 1000));
        uref_free(uref);
        return true;
    } else if (now > systime + SYSTIME_PRINT)
        upipe_warn_va(upipe,
                      "outputting late packet %"PRIu64" ms, latency %"PRIu64" ms",
                      (now - systime) / (UCLOCK_FREQ / 1000),
                      upipe_udpsink->latency / (UCLOCK_FREQ / 1000));

write_buffer:
    for ( ; ; ) {
        size_t payload_len = 0;
        if (unlikely(!ubase_check(uref_block_size(uref, &payload_len)))) {
            upipe_warn(upipe, "cannot read ubuf size");
            return false;
        }

        int iovec_count = uref_block_iovec_count(uref, 0, -1);
        if (unlikely(iovec_count == -1)) {
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            break;
        }
        if (unlikely(iovec_count == 0)) {
            uref_free(uref);
            break;
        }

        if (upipe_udpsink->raw) {
            iovec_count++;
        }

        struct iovec iovecs_s[iovec_count];
        struct iovec *iovecs = iovecs_s;

        if (upipe_udpsink->raw) {
            udp_raw_set_len(upipe_udpsink->raw_header, payload_len);
            iovecs[0].iov_base = upipe_udpsink->raw_header;
            iovecs[0].iov_len = RAW_HEADER_SIZE;
            iovecs++;
        }

        if (unlikely(!ubase_check(uref_block_iovec_read(uref, 0, -1, iovecs)))) {
            uref_free(uref);
            upipe_warn(upipe, "cannot read ubuf buffer");
            break;
        }

        ssize_t ret = writev(upipe_udpsink->fd, iovecs_s, iovec_count);
        uref_block_iovec_unmap(uref, 0, -1, iovecs);

        if (unlikely(ret == -1)) {
            switch (errno) {
                case EINTR:
                    continue;
                case EAGAIN:
#if EAGAIN != EWOULDBLOCK
                case EWOULDBLOCK:
#endif
                    upipe_udpsink_poll(upipe);
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

        uref_free(uref);
        break;
    }
    return true;
}

/** @internal @This is called when the file descriptor can be written again.
 * Unblock the sink and unqueue all queued buffers.
 *
 * @param upump description structure of the watcher
 */
static void upipe_udpsink_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_udpsink_set_upump(upipe, NULL);
    upipe_udpsink_output_input(upipe);
    upipe_udpsink_unblock_input(upipe);
    if (upipe_udpsink_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_udpsink_input. */
        upipe_release(upipe);
    }
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
    if (!upipe_udpsink_check_input(upipe)) {
        upipe_udpsink_hold_input(upipe, uref);
        upipe_udpsink_block_input(upipe, upump_p);
    } else if (!upipe_udpsink_output(upipe, uref, upump_p)) {
        upipe_udpsink_hold_input(upipe, uref);
        upipe_udpsink_block_input(upipe, upump_p);
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
static int upipe_udpsink_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
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
static int _upipe_udpsink_set_uri(struct upipe *upipe, const char *uri,
                                  enum upipe_udpsink_mode mode)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    bool use_tcp = false;

    if (unlikely(upipe_udpsink->fd != -1)) {
        if (likely(upipe_udpsink->uri != NULL))
            upipe_notice_va(upipe, "closing socket %s", upipe_udpsink->uri);
        close(upipe_udpsink->fd);
    }
    ubase_clean_str(&upipe_udpsink->uri);
    upipe_udpsink_set_upump(upipe, NULL);
    if (!upipe_udpsink_check_input(upipe))
        /* Release the pipe used in @ref upipe_udpsink_input. */
        upipe_release(upipe);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_udpsink_check_upump_mgr(upipe);

    const char *mode_desc = NULL; /* hush gcc */
    switch (mode) {
        case UPIPE_UDPSINK_NONE:
            mode_desc = "none";
            break;
        default:
            upipe_err_va(upipe, "invalid mode %d", mode);
            return UBASE_ERR_INVALID;
    }
    upipe_udpsink->fd = upipe_udp_open_socket(upipe, uri,
            UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp,
            &upipe_udpsink->raw, upipe_udpsink->raw_header);

    if (unlikely(upipe_udpsink->fd == -1)) {
        upipe_err_va(upipe, "can't open uri %s (%s)", uri, mode_desc);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_udpsink->uri = strdup(uri);
    if (unlikely(upipe_udpsink->uri == NULL)) {
        ubase_clean_fd(&upipe_udpsink->fd);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (!upipe_udpsink_check_input(upipe))
        /* Use again the pipe that we previously released. */
        upipe_use(upipe);
    upipe_notice_va(upipe, "opening uri %s in %s mode",
                    upipe_udpsink->uri, mode_desc);
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
    if (upipe_udpsink_flush_input(upipe)) {
        upipe_udpsink_set_upump(upipe, NULL);
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_udpsink_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a udp sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_udpsink_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_udpsink_set_upump(upipe, NULL);
            return upipe_udpsink_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_udpsink_set_upump(upipe, NULL);
            upipe_udpsink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_udpsink_set_flow_def(upipe, flow_def);
        }

        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_udpsink_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_udpsink_set_max_length(upipe, max_length);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return _upipe_udpsink_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return _upipe_udpsink_set_uri(upipe, uri, UPIPE_UDPSINK_NONE);
        }

        case UPIPE_UDPSINK_GET_URI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSINK_SIGNATURE)
            const char **uri_p = va_arg(args, const char **);
            return _upipe_udpsink_get_uri(upipe, uri_p);
        }
        case UPIPE_UDPSINK_SET_URI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSINK_SIGNATURE)
            const char *uri = va_arg(args, const char *);
            enum upipe_udpsink_mode mode = va_arg(args, enum upipe_udpsink_mode);
            return _upipe_udpsink_set_uri(upipe, uri, mode);
        }
        case UPIPE_FLUSH:
            return upipe_udpsink_flush(upipe);
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a udp sink pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_udpsink_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_udpsink_control(upipe, command, args));

    if (unlikely(!upipe_udpsink_check_input(upipe)))
        upipe_udpsink_poll(upipe);

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsink_free(struct upipe *upipe)
{
    struct upipe_udpsink *upipe_udpsink = upipe_udpsink_from_upipe(upipe);
    if (likely(upipe_udpsink->fd != -1)) {
        if (likely(upipe_udpsink->uri != NULL))
            upipe_notice_va(upipe, "closing socket %s", upipe_udpsink->uri);
        close(upipe_udpsink->fd);
    }
    upipe_throw_dead(upipe);

    free(upipe_udpsink->uri);
    upipe_udpsink_clean_uclock(upipe);
    upipe_udpsink_clean_upump(upipe);
    upipe_udpsink_clean_upump_mgr(upipe);
    upipe_udpsink_clean_input(upipe);
    upipe_udpsink_clean_urefcount(upipe);
    upipe_udpsink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_udpsink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_UDPSINK_SIGNATURE,

    .upipe_alloc = upipe_udpsink_alloc,
    .upipe_input = upipe_udpsink_input,
    .upipe_control = upipe_udpsink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all udp sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsink_mgr_alloc(void)
{
    return &upipe_udpsink_mgr;
}

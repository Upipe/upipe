/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe source module for udp sockets
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_source_read_size.h>
#include <upipe-modules/upipe_udp_source.h>
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
#include <sys/socket.h>

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096

#define UDP_DEFAULT_TTL 0
#define UDP_DEFAULT_PORT 1234

/** @internal @This is the private context of a udp socket source pipe. */
struct upipe_udpsrc {
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** read size */
    unsigned int read_size;

    /** udp socket descriptor */
    int fd;
    /** udp socket uri */
    char *uri;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_udpsrc, upipe)
UPIPE_HELPER_VOID(upipe_udpsrc)
UPIPE_HELPER_UREF_MGR(upipe_udpsrc, uref_mgr)

UPIPE_HELPER_UBUF_MGR(upipe_udpsrc, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_udpsrc, output, flow_def, flow_def_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_udpsrc, upump_mgr, upump)
UPIPE_HELPER_UCLOCK(upipe_udpsrc, uclock)
UPIPE_HELPER_SOURCE_READ_SIZE(upipe_udpsrc, read_size)

/** @internal @This allocates a udp socket source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_udpsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_udpsrc_alloc_void(mgr, uprobe, signature, args);
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    upipe_udpsrc_init_uref_mgr(upipe);
    upipe_udpsrc_init_ubuf_mgr(upipe);
    upipe_udpsrc_init_output(upipe);
    upipe_udpsrc_init_upump_mgr(upipe);
    upipe_udpsrc_init_uclock(upipe);
    upipe_udpsrc_init_read_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_udpsrc->fd = -1;
    upipe_udpsrc->uri = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the udp socket descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_udpsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    uint64_t systime = 0; /* to keep gcc quiet */
    if (unlikely(upipe_udpsrc->uclock != NULL))
        systime = uclock_now(upipe_udpsrc->uclock);

    struct uref *uref = uref_block_alloc(upipe_udpsrc->uref_mgr,
                                         upipe_udpsrc->ubuf_mgr,
                                         upipe_udpsrc->read_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_aerror(upipe);
        return;
    }

    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!uref_block_write(uref, 0, &read_size, &buffer))) {
        uref_free(uref);
        upipe_throw_aerror(upipe);
        return;
    }
    assert(read_size == upipe_udpsrc->read_size);

    ssize_t ret = read(upipe_udpsrc->fd, buffer, upipe_udpsrc->read_size);
    uref_block_unmap(uref, 0);

    if (unlikely(ret == -1)) {
        uref_free(uref);
        switch (errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* not an issue, try again later */
                return;
            case EBADF:
            case EINVAL:
            case EIO:
            default:
                break;
        }
        upipe_err_va(upipe, "read error from %s (%m)", upipe_udpsrc->uri);
        upipe_udpsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }
    if (unlikely(ret == 0)) {
        uref_free(uref);
        if (likely(upipe_udpsrc->uclock == NULL)) {
            upipe_notice_va(upipe, "end of udp socket %s", upipe_udpsrc->uri);
            upipe_udpsrc_set_upump(upipe, NULL);
            upipe_throw_source_end(upipe);
        }
        return;
    }
    if (unlikely(upipe_udpsrc->uclock != NULL))
        uref_clock_set_systime(uref, systime);
    if (unlikely(ret != upipe_udpsrc->read_size))
        uref_block_resize(uref, 0, ret);
    upipe_udpsrc_output(upipe, uref, upump);
}

/** @internal @This returns the uri of the currently opened udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the udp socket
 * @return false in case of error
 */
static bool _upipe_udpsrc_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_udpsrc->uri;
    return true;
}

/** @internal @This asks to open the given udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the udp socket
 * @return false in case of error
 */
static bool _upipe_udpsrc_set_uri(struct upipe *upipe, const char *uri)
{
    bool use_tcp = 0;
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);

    if (unlikely(upipe_udpsrc->fd != -1)) {
        if (likely(upipe_udpsrc->uri != NULL)) {
            upipe_notice_va(upipe, "closing udp socket %s", upipe_udpsrc->uri);
        }
        close(upipe_udpsrc->fd);
        upipe_udpsrc->fd = -1;
    }
    free(upipe_udpsrc->uri);
    upipe_udpsrc->uri = NULL;
    upipe_udpsrc_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return true;

    if (upipe_udpsrc->uref_mgr == NULL) {
        upipe_throw_need_uref_mgr(upipe);
        if (unlikely(upipe_udpsrc->uref_mgr == NULL))
            return false;
    }
    if (upipe_udpsrc->flow_def == NULL) {
        struct uref *flow_def =
            uref_block_flow_alloc_def(upipe_udpsrc->uref_mgr, NULL);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_aerror(upipe);
            return false;
        }
        upipe_udpsrc_store_flow_def(upipe, flow_def);
    }
    if (upipe_udpsrc->upump_mgr == NULL) {
        upipe_throw_need_upump_mgr(upipe);
        if (unlikely(upipe_udpsrc->upump_mgr == NULL))
            return false;
    }
    if (upipe_udpsrc->ubuf_mgr == NULL) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_udpsrc->flow_def);
        if (unlikely(upipe_udpsrc->ubuf_mgr == NULL))
            return false;
    }

    upipe_udpsrc->fd = upipe_udp_open_socket(upipe, uri,
            UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp);
    if (unlikely(upipe_udpsrc->fd == -1)) {
        upipe_err_va(upipe, "can't open udp socket %s (%m)", uri);
        return false;
    }

    upipe_udpsrc->uri = strdup(uri);
    if (unlikely(upipe_udpsrc->uri == NULL)) {
        close(upipe_udpsrc->fd);
        upipe_udpsrc->fd = -1;
        upipe_throw_aerror(upipe);
        return false;
    }
    upipe_notice_va(upipe, "opening udp socket %s", upipe_udpsrc->uri);
    return true;
}

/** @internal @This processes control commands on a udp socket source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_udpsrc_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_udpsrc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_udpsrc_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_udpsrc_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_udpsrc_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_udpsrc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_udpsrc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_udpsrc_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_udpsrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
            if (upipe_udpsrc->upump != NULL)
                upipe_udpsrc_set_upump(upipe, NULL);
            return upipe_udpsrc_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_udpsrc_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return upipe_udpsrc_set_uclock(upipe, uclock);
        }
        case UPIPE_SOURCE_GET_READ_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_udpsrc_get_read_size(upipe, p);
        }
        case UPIPE_SOURCE_SET_READ_SIZE: {
            unsigned int read_size = va_arg(args, unsigned int);
            return upipe_udpsrc_set_read_size(upipe, read_size);
        }

        case UPIPE_UDPSRC_GET_URI: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_UDPSRC_SIGNATURE);
            const char **uri_p = va_arg(args, const char **);
            return _upipe_udpsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_UDPSRC_SET_URI: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_UDPSRC_SIGNATURE);
            const char *uri = va_arg(args, const char *);
            return _upipe_udpsrc_set_uri(upipe, uri);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a udp socket source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_udpsrc_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    if (unlikely(!_upipe_udpsrc_control(upipe, command, args)))
        return false;

    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    if (upipe_udpsrc->upump_mgr != NULL && upipe_udpsrc->fd != -1 &&
        upipe_udpsrc->upump == NULL) {
        struct upump *upump = upump_alloc_fd_read(upipe_udpsrc->upump_mgr,
                                                  upipe_udpsrc_worker, upipe,
                                                  upipe_udpsrc->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_upump_error(upipe);
            return false;
        }
        upipe_udpsrc_set_upump(upipe, upump);
        upump_start(upump);
    }

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsrc_free(struct upipe *upipe)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);

    if (likely(upipe_udpsrc->fd != -1)) {
        if (likely(upipe_udpsrc->uri != NULL))
            upipe_notice_va(upipe, "closing udp socket %s", upipe_udpsrc->uri);
        close(upipe_udpsrc->fd);
    }

    upipe_throw_dead(upipe);

    free(upipe_udpsrc->uri);
    upipe_udpsrc_clean_read_size(upipe);
    upipe_udpsrc_clean_uclock(upipe);
    upipe_udpsrc_clean_upump_mgr(upipe);
    upipe_udpsrc_clean_output(upipe);
    upipe_udpsrc_clean_ubuf_mgr(upipe);
    upipe_udpsrc_clean_uref_mgr(upipe);
    upipe_udpsrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_udpsrc_mgr = {
    .signature = UPIPE_UDPSRC_SIGNATURE,

    .upipe_alloc = upipe_udpsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_udpsrc_control,
    .upipe_free = upipe_udpsrc_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all udp socket sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsrc_mgr_alloc(void)
{
    return &upipe_udpsrc_mgr;
}

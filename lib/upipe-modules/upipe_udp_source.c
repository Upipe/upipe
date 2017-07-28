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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output_size.h>
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

/** @hidden */
static int upipe_udpsrc_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a udp socket source pipe. */
struct upipe_udpsrc {
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
    /** read size */
    unsigned int output_size;

    /** udp socket descriptor */
    int fd;
    /** udp socket uri */
    char *uri;

    /** source address */
    struct sockaddr_storage addr;
    /** source address (size) */
    socklen_t addrlen;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_udpsrc, upipe, UPIPE_UDPSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_udpsrc, urefcount, upipe_udpsrc_free)
UPIPE_HELPER_VOID(upipe_udpsrc)

UPIPE_HELPER_OUTPUT(upipe_udpsrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_udpsrc, uref_mgr, uref_mgr_request,
                      upipe_udpsrc_check,
                      upipe_udpsrc_register_output_request,
                      upipe_udpsrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_udpsrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_udpsrc_check,
                      upipe_udpsrc_register_output_request,
                      upipe_udpsrc_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_udpsrc, uclock, uclock_request, upipe_udpsrc_check,
                    upipe_udpsrc_register_output_request,
                    upipe_udpsrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_udpsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_udpsrc, upump, upump_mgr)
UPIPE_HELPER_OUTPUT_SIZE(upipe_udpsrc, output_size)

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
    upipe_udpsrc_init_urefcount(upipe);
    upipe_udpsrc_init_uref_mgr(upipe);
    upipe_udpsrc_init_ubuf_mgr(upipe);
    upipe_udpsrc_init_output(upipe);
    upipe_udpsrc_init_upump_mgr(upipe);
    upipe_udpsrc_init_upump(upipe);
    upipe_udpsrc_init_uclock(upipe);
    upipe_udpsrc_init_output_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_udpsrc->fd = -1;
    upipe_udpsrc->uri = NULL;
    upipe_udpsrc->addrlen = 0;
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
                                         upipe_udpsrc->output_size);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int output_size = -1;
    if (unlikely(!ubase_check(uref_block_write(uref, 0, &output_size,
                                               &buffer)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    assert(output_size == upipe_udpsrc->output_size);

    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

    ssize_t ret = recvfrom(upipe_udpsrc->fd, buffer, upipe_udpsrc->output_size,
                        0, (struct sockaddr*)&addr, &addrlen);
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
    } else if (addrlen != upipe_udpsrc->addrlen ||
        memcmp(&addr, &upipe_udpsrc->addr, addrlen)) {
        upipe_throw(upipe, UPROBE_UDPSRC_NEW_PEER, UPIPE_UDPSRC_SIGNATURE,
                &addr, &addrlen);
        upipe_udpsrc->addrlen = addrlen;
        memcpy(&upipe_udpsrc->addr, &addr, addrlen);
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
        uref_clock_set_cr_sys(uref, systime);
    if (unlikely(ret != upipe_udpsrc->output_size))
        uref_block_resize(uref, 0, ret);
    upipe_udpsrc_output(upipe, uref, &upipe_udpsrc->upump);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_udpsrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_udpsrc_store_flow_def(upipe, flow_format);

    upipe_udpsrc_check_upump_mgr(upipe);
    if (upipe_udpsrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_udpsrc->uref_mgr == NULL) {
        upipe_udpsrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_udpsrc->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_udpsrc->uref_mgr, NULL);
        uref_block_flow_set_size(flow_format, upipe_udpsrc->output_size);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_udpsrc_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_udpsrc->uclock == NULL &&
        urequest_get_opaque(&upipe_udpsrc->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_udpsrc->fd != -1 && upipe_udpsrc->upump == NULL) {
        struct upump *upump;
        upump = upump_alloc_fd_read(upipe_udpsrc->upump_mgr,
                                    upipe_udpsrc_worker, upipe, upipe->refcount,
                                    upipe_udpsrc->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_udpsrc_set_upump(upipe, upump);
        upump_start(upump);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the uri of the currently opened udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the udp socket
 * @return an error code
 */
static int upipe_udpsrc_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_udpsrc->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the udp socket
 * @return an error code
 */
static int upipe_udpsrc_set_uri(struct upipe *upipe, const char *uri)
{
    bool use_tcp = 0;
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);

    if (unlikely(upipe_udpsrc->fd != -1)) {
        if (likely(upipe_udpsrc->uri != NULL)) {
            upipe_notice_va(upipe, "closing udp socket %s", upipe_udpsrc->uri);
        }
        ubase_clean_fd(&upipe_udpsrc->fd);
    }
    ubase_clean_str(&upipe_udpsrc->uri);
    upipe_udpsrc_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    upipe_udpsrc->fd = upipe_udp_open_socket(upipe, uri,
            UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp, NULL, NULL);
    if (unlikely(upipe_udpsrc->fd == -1)) {
        upipe_err_va(upipe, "can't open udp socket %s (%m)", uri);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_udpsrc->uri = strdup(uri);
    if (unlikely(upipe_udpsrc->uri == NULL)) {
        ubase_clean_fd(&upipe_udpsrc->fd);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_notice_va(upipe, "opening udp socket %s", upipe_udpsrc->uri);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a udp socket source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_udpsrc_control(struct upipe *upipe,
                                 int command, va_list args)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);

    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_udpsrc_set_upump(upipe, NULL);
            return upipe_udpsrc_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_udpsrc_set_upump(upipe, NULL);
            upipe_udpsrc_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_udpsrc_get_output_size(upipe, p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int output_size = va_arg(args, unsigned int);
            return upipe_udpsrc_set_output_size(upipe, output_size);
        }
        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_udpsrc_control_output(upipe, command, args);


        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_udpsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_udpsrc_set_uri(upipe, uri);
        }
        case UPIPE_UDPSRC_GET_FD: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSRC_SIGNATURE)
            int *fd = va_arg(args, int *);
            *fd = upipe_udpsrc->fd;
            return UBASE_ERR_NONE;
        }
        case UPIPE_UDPSRC_SET_FD: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_UDPSRC_SIGNATURE)
            upipe_udpsrc_set_upump(upipe, NULL);
            upipe_udpsrc->fd = va_arg(args, int );
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a udp socket source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_udpsrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_udpsrc_control(upipe, command, args));

    return upipe_udpsrc_check(upipe, NULL);
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
    upipe_udpsrc_clean_output_size(upipe);
    upipe_udpsrc_clean_uclock(upipe);
    upipe_udpsrc_clean_upump(upipe);
    upipe_udpsrc_clean_upump_mgr(upipe);
    upipe_udpsrc_clean_output(upipe);
    upipe_udpsrc_clean_ubuf_mgr(upipe);
    upipe_udpsrc_clean_uref_mgr(upipe);
    upipe_udpsrc_clean_urefcount(upipe);
    upipe_udpsrc_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_udpsrc_mgr = {
    .refcount = NULL,
    .signature = UPIPE_UDPSRC_SIGNATURE,

    .upipe_alloc = upipe_udpsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_udpsrc_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all udp socket sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsrc_mgr_alloc(void)
{
    return &upipe_udpsrc_mgr;
}

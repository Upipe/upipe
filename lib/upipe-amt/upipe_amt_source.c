/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe source module for automatic multicat tunneling
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
#include <upipe-amt/upipe_amt_source.h>

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
#include <netinet/in.h>
#include <arpa/inet.h>

#include <amt.h>

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096

/** @hidden */
static int upipe_amtsrc_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a amtsrc manager. */
struct upipe_amtsrc_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** AMT gateway IP */
    struct in_addr amt_addr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_amtsrc_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_amtsrc_mgr, urefcount, urefcount, urefcount)

/** @internal @This is the private context of a amtsrc pipe. */
struct upipe_amtsrc {
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

    /** AMT handle */
    amt_handle_t handle;
    /** udp socket uri */
    char *uri;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_amtsrc, upipe, UPIPE_AMTSRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_amtsrc, urefcount, upipe_amtsrc_free)
UPIPE_HELPER_VOID(upipe_amtsrc)

UPIPE_HELPER_OUTPUT(upipe_amtsrc, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_amtsrc, uref_mgr, uref_mgr_request,
                      upipe_amtsrc_check,
                      upipe_amtsrc_register_output_request,
                      upipe_amtsrc_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_amtsrc, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_amtsrc_check,
                      upipe_amtsrc_register_output_request,
                      upipe_amtsrc_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_amtsrc, uclock, uclock_request, upipe_amtsrc_check,
                    upipe_amtsrc_register_output_request,
                    upipe_amtsrc_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_amtsrc, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_amtsrc, upump, upump_mgr)
UPIPE_HELPER_OUTPUT_SIZE(upipe_amtsrc, output_size)

/** @internal @This allocates a amtsrc pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_amtsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_amtsrc_alloc_void(mgr, uprobe, signature, args);
    struct upipe_amtsrc *upipe_amtsrc = upipe_amtsrc_from_upipe(upipe);
    upipe_amtsrc_init_urefcount(upipe);
    upipe_amtsrc_init_uref_mgr(upipe);
    upipe_amtsrc_init_ubuf_mgr(upipe);
    upipe_amtsrc_init_output(upipe);
    upipe_amtsrc_init_upump_mgr(upipe);
    upipe_amtsrc_init_upump(upipe);
    upipe_amtsrc_init_uclock(upipe);
    upipe_amtsrc_init_output_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_amtsrc->handle = NULL;
    upipe_amtsrc->uri = NULL;
    upipe_throw_ready(upipe);

    upipe_dbg_va(upipe, "using amt library version %x", amt_getVer());
    return upipe;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the udp socket descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_amtsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_amtsrc *upipe_amtsrc = upipe_amtsrc_from_upipe(upipe);
    amt_read_event_t ars[1];
    ars[0].handle = upipe_amtsrc->handle;
    ars[0].rstate = AMT_READ_NONE;

    /* Implementation note: libamt is synchronous, so we have to poll for
     * input. The timeout is set as low as possible to 1 ms. */
    if (unlikely(amt_poll(ars, 1, 1) < 0)) {
        upipe_err_va(upipe, "poll error from %s", upipe_amtsrc->uri);
        upipe_amtsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    if (unlikely((ars[0].rstate & AMT_READ_CLOSE) ||
                 (ars[0].rstate & AMT_READ_ERR))) {
        upipe_err_va(upipe, "end of %s", upipe_amtsrc->uri);
        upipe_amtsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    if (!(ars[0].rstate & AMT_READ_IN))
        return;

    uint64_t systime = 0; /* to keep gcc quiet */
    if (unlikely(upipe_amtsrc->uclock != NULL))
        systime = uclock_now(upipe_amtsrc->uclock);

    struct uref *uref = uref_block_alloc(upipe_amtsrc->uref_mgr,
                                         upipe_amtsrc->ubuf_mgr,
                                         upipe_amtsrc->output_size);
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
    assert(output_size == upipe_amtsrc->output_size);

    int ret = amt_recvfrom(upipe_amtsrc->handle, buffer,
                           upipe_amtsrc->output_size);
    uref_block_unmap(uref, 0);

    if (unlikely(ret == 0)) {
        uref_free(uref);
        upipe_err_va(upipe, "read error from %s", upipe_amtsrc->uri);
        upipe_amtsrc_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }
    if (unlikely(upipe_amtsrc->uclock != NULL))
        uref_clock_set_cr_sys(uref, systime);
    if (unlikely(ret != upipe_amtsrc->output_size))
        uref_block_resize(uref, 0, ret);
    upipe_use(upipe);
    upipe_amtsrc_output(upipe, uref, &upipe_amtsrc->upump);
    upipe_release(upipe);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_amtsrc_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_amtsrc *upipe_amtsrc = upipe_amtsrc_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_amtsrc_store_flow_def(upipe, flow_format);

    upipe_amtsrc_check_upump_mgr(upipe);
    if (upipe_amtsrc->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_amtsrc->uref_mgr == NULL) {
        upipe_amtsrc_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_amtsrc->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_amtsrc->uref_mgr, NULL);
        uref_block_flow_set_size(flow_format, upipe_amtsrc->output_size);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_amtsrc_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_amtsrc->uclock == NULL &&
        urequest_get_opaque(&upipe_amtsrc->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_amtsrc->handle != NULL && upipe_amtsrc->upump == NULL) {
        struct upump *upump = upump_alloc_idler(upipe_amtsrc->upump_mgr,
                                                upipe_amtsrc_worker, upipe);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_amtsrc_set_upump(upipe, upump);
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
static int upipe_amtsrc_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_amtsrc *upipe_amtsrc = upipe_amtsrc_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_amtsrc->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the udp socket
 * @return an error code
 */
static int upipe_amtsrc_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_amtsrc *upipe_amtsrc = upipe_amtsrc_from_upipe(upipe);
    struct upipe_amtsrc_mgr *amtsrc_mgr =
        upipe_amtsrc_mgr_from_upipe_mgr(upipe->mgr);

    if (unlikely(upipe_amtsrc->handle != NULL)) {
        if (likely(upipe_amtsrc->uri != NULL)) {
            upipe_notice_va(upipe, "closing %s", upipe_amtsrc->uri);
        }
        amt_closeChannel(upipe_amtsrc->handle);
        upipe_amtsrc->handle = NULL;
    }
    free(upipe_amtsrc->uri);
    upipe_amtsrc->uri = NULL;
    upipe_amtsrc_set_upump(upipe, NULL);

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    struct in_addr multicast_addr, source_addr;
    uint16_t port_number = 0;
    multicast_addr.s_addr = source_addr.s_addr = INADDR_ANY;

    char *string = strdup(uri);
    if (string == NULL)
        return UBASE_ERR_ALLOC;

    amt_connect_req_e mode;
    if (!ubase_ncmp(string, "ssm://"))
        mode = AMT_CONNECT_REQ_SSM;
    else if (!ubase_ncmp(string, "amt://"))
        mode = AMT_CONNECT_REQ_RELAY;
    else if (!ubase_ncmp(string, "any://"))
        mode = AMT_CONNECT_REQ_ANY;
    else {
        upipe_err_va(upipe, "unknown URI %s", string);
        goto upipe_amtsrc_set_uri_err;
    }

    char *source = string + 6;
    char *multicast = strchr(source, '@');

    if (multicast != NULL) {
        *multicast++ = '\0';
        char *port = strchr(multicast, ':');
        if (port != NULL) {
            *port++ = '\0';
            port_number = strtoul(port, &port, 10);
            if (*port != '\0') {
                upipe_err_va(upipe, "invalid port");
                goto upipe_amtsrc_set_uri_err;
            }
        }
        if (!inet_aton(multicast, &multicast_addr)) {
            upipe_err_va(upipe, "invalid multicast address");
            goto upipe_amtsrc_set_uri_err;
        }
    }
    if (!inet_aton(source, &source_addr)) {
        upipe_err_va(upipe, "invalid source address");
        goto upipe_amtsrc_set_uri_err;
    }
    free(string);

    upipe_amtsrc->handle = amt_openChannel(htonl(amtsrc_mgr->amt_addr.s_addr),
            htonl(multicast_addr.s_addr), htonl(source_addr.s_addr),
            port_number, mode);
    if (unlikely(upipe_amtsrc->handle == NULL)) {
        upipe_err_va(upipe, "can't open %s", uri);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_amtsrc->uri = strdup(uri);
    upipe_notice_va(upipe, "opening %s", uri);
    return UBASE_ERR_NONE;

upipe_amtsrc_set_uri_err:
    free(string);
    return UBASE_ERR_INVALID;
}

/** @internal @This processes control commands on a amtsrc pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_amtsrc_control(struct upipe *upipe,
                                 int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_amtsrc_set_upump(upipe, NULL);
            return upipe_amtsrc_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_amtsrc_set_upump(upipe, NULL);
            upipe_amtsrc_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_amtsrc_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_amtsrc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_amtsrc_set_output(upipe, output);
        }

        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_amtsrc_get_output_size(upipe, p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int output_size = va_arg(args, unsigned int);
            return upipe_amtsrc_set_output_size(upipe, output_size);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_amtsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_amtsrc_set_uri(upipe, uri);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a amtsrc pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_amtsrc_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_amtsrc_control(upipe, command, args));

    return upipe_amtsrc_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_amtsrc_free(struct upipe *upipe)
{
    struct upipe_amtsrc *upipe_amtsrc = upipe_amtsrc_from_upipe(upipe);

    if (likely(upipe_amtsrc->handle != NULL)) {
        if (likely(upipe_amtsrc->uri != NULL))
            upipe_notice_va(upipe, "closing %s", upipe_amtsrc->uri);
        amt_closeChannel(upipe_amtsrc->handle);
    }

    upipe_throw_dead(upipe);

    free(upipe_amtsrc->uri);
    upipe_amtsrc_clean_output_size(upipe);
    upipe_amtsrc_clean_uclock(upipe);
    upipe_amtsrc_clean_upump(upipe);
    upipe_amtsrc_clean_upump_mgr(upipe);
    upipe_amtsrc_clean_output(upipe);
    upipe_amtsrc_clean_ubuf_mgr(upipe);
    upipe_amtsrc_clean_uref_mgr(upipe);
    upipe_amtsrc_clean_urefcount(upipe);
    upipe_amtsrc_free_void(upipe);
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_amtsrc_mgr_free(struct urefcount *urefcount)
{
    struct upipe_amtsrc_mgr *amtsrc_mgr =
        upipe_amtsrc_mgr_from_urefcount(urefcount);

    urefcount_clean(urefcount);
    free(amtsrc_mgr);
    amt_reset();
}

/** @This processes control commands on a amtsrc manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_amtsrc_mgr_control(struct upipe_mgr *mgr,
                                    int command, va_list args)
{
    switch (command) {
        case UPIPE_AMTSRC_MGR_SET_TIMEOUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AMTSRC_SIGNATURE)
            unsigned int timeout = va_arg(args, unsigned int);
            /* no longer present in libamt */
            /* amt_timeOut(timeout); */
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all amtsrc pipes.
 *
 * @param amt_relay IP of the AMT gateway
 * @return pointer to manager
 */
struct upipe_mgr *upipe_amtsrc_mgr_alloc(const char *amt_relay)
{
    struct upipe_amtsrc_mgr *amtsrc_mgr =
        malloc(sizeof(struct upipe_amtsrc_mgr));
    if (unlikely(amtsrc_mgr == NULL))
        return NULL;

    if (!inet_aton(amt_relay, &amtsrc_mgr->amt_addr) ||
        amt_init(htonl(amtsrc_mgr->amt_addr.s_addr))) {
        free(amtsrc_mgr);
        return NULL;
    }

    urefcount_init(upipe_amtsrc_mgr_to_urefcount(amtsrc_mgr),
                   upipe_amtsrc_mgr_free);
    amtsrc_mgr->mgr.refcount = upipe_amtsrc_mgr_to_urefcount(amtsrc_mgr);
    amtsrc_mgr->mgr.signature = UPIPE_AMTSRC_SIGNATURE;
    amtsrc_mgr->mgr.upipe_alloc = upipe_amtsrc_alloc;
    amtsrc_mgr->mgr.upipe_input = NULL;
    amtsrc_mgr->mgr.upipe_control = upipe_amtsrc_control;
    amtsrc_mgr->mgr.upipe_mgr_control = upipe_amtsrc_mgr_control;
    return upipe_amtsrc_mgr_to_upipe_mgr(amtsrc_mgr);
}

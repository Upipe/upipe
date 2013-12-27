/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
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
 * @short Upipe source module for http GET requests
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
#include <upipe/upipe_helper_source_read_size.h>
#include <upipe-modules/upipe_http_source.h>

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
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>

#include "http-parser/http_parser.h"

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096

#define MAX_URL_SIZE            2048
#define USER_AGENT              "upipe_http_src"
static const char get_request_format[] = 
    "GET %s HTTP/1.0\n"
    "User-Agent: %s\n"
    "\n";

/** @internal @This is the private context of a http source pipe. */
struct upipe_http_src {
    /** refcount management structure */
    struct urefcount urefcount;

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
    struct upump *upump_read;
    /** write watcher */
    struct upump *upump_write;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** read size */
    unsigned int read_size;

    /** socket descriptor */
    int fd;
    /** http url */
    char *url;

    /** http parser*/
    http_parser parser;

    /** http parser settings */
    http_parser_settings parser_settings;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_http_src, upipe, UPIPE_HTTP_SRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_http_src, urefcount, upipe_http_src_free)
UPIPE_HELPER_VOID(upipe_http_src)
UPIPE_HELPER_UREF_MGR(upipe_http_src, uref_mgr)

UPIPE_HELPER_UBUF_MGR(upipe_http_src, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_http_src, output, flow_def, flow_def_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_http_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_read, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_http_src, uclock)
UPIPE_HELPER_SOURCE_READ_SIZE(upipe_http_src, read_size)

/** @internal @This allocates a http source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_http_src_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_http_src_alloc_void(mgr, uprobe, signature,
                                                    args);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    upipe_http_src_init_urefcount(upipe);
    upipe_http_src_init_uref_mgr(upipe);
    upipe_http_src_init_ubuf_mgr(upipe);
    upipe_http_src_init_output(upipe);
    upipe_http_src_init_upump_mgr(upipe);
    upipe_http_src_init_upump_read(upipe);
    upipe_http_src_init_uclock(upipe);
    upipe_http_src_init_read_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_http_src->fd = -1;
    upipe_http_src->url = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This retrieves the upipe_http_src structure from parser
 * @param parser http parser structure
 * @return pointer to upipe_http_src private structure
 */
static inline struct upipe_http_src *upipe_http_src_from_parser(http_parser *parser)
{
    return container_of(parser, struct upipe_http_src, parser);
}

/** @internal @This is called by http_parser when receiving fragments of body
 * @param parser http parser structure
 * @param at data buffer
 * @param len data length
 * @return 0
 */
static int upipe_http_src_body_cb(http_parser *parser, const char *at, size_t len)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    struct upipe *upipe = upipe_http_src_to_upipe(upipe_http_src);
    struct uref *uref;
    uint64_t systime = 0;
    uint8_t *buf = NULL;
    int size;

    /* fetch systime */
    if (likely(upipe_http_src->uclock)) {
        systime = uclock_now(upipe_http_src->uclock);
    }

    upipe_dbg_va(upipe, "received %zu bytes of body", len);

    /* alloc, map, copy, unmap */
    uref = uref_block_alloc(upipe_http_src->uref_mgr,
                            upipe_http_src->ubuf_mgr, len);
    if (unlikely(!uref)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return 0;
    }
    size = -1;
    uref_block_write(uref, 0, &size, &buf);
    assert(len == size);
    memcpy(buf, at, len);
    uref_block_unmap(uref, 0);

    uref_clock_set_cr_sys(uref, systime);
    upipe_http_src_output(upipe, uref, upipe_http_src->upump_read);

    /* everything's fine, return 0 to http_parser */
    return 0;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the http descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_http_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    char *buffer = malloc(upipe_http_src->read_size); /* FIXME use ubuf/umem */
    assert(buffer);
    memset(buffer, 0, upipe_http_src->read_size);

    ssize_t len = recv(upipe_http_src->fd, buffer, upipe_http_src->read_size, 0);

    if (unlikely(len == -1)) {
        free(buffer);

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
        upipe_err_va(upipe, "read error from %s (%s)", upipe_http_src->url,
                                                              strerror(errno));
        upipe_http_src_set_upump_read(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }
    if (unlikely(len == 0)) {
        free(buffer);
        upipe_notice_va(upipe, "end of %s", upipe_http_src->url);
        upipe_http_src_set_upump_read(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    /* parse response */
    http_parser_execute(&upipe_http_src->parser,
                        &upipe_http_src->parser_settings, buffer, len);
    free(buffer);
}

/** @internal @This returns the url of the currently opened http.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the url of the http
 * @return false in case of error
 */
static bool upipe_http_src_get_uri(struct upipe *upipe, const char **url_p)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    assert(url_p != NULL);
    *url_p = upipe_http_src->url;
    return true;
}

/** @internal @This asks to open the given http (real code here).
 *
 * @param upipe description structure of the pipe
 * @param url relative or absolute url of the http
 * @return socket fd or -1 in case of error
 */
static int upipe_http_src_open_url(struct upipe *upipe, const char *url)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    http_parser *parser = &upipe_http_src->parser;
    http_parser_settings *settings = &upipe_http_src->parser_settings;
    struct http_parser_url parsed_url;

    struct addrinfo *info = NULL, *res;
    struct addrinfo hints;
    char *host, *service;
    int ret, fd = -1;

    /* check url size */
    if (unlikely(strnlen(url, MAX_URL_SIZE + 1) > MAX_URL_SIZE)) {
        upipe_err(upipe, "url too large, something's fishy");
        return -1;
    }

    /* init parser and settings */
    http_parser_init(parser, HTTP_RESPONSE);
    settings->on_message_begin = NULL;
	settings->on_url = NULL;
	settings->on_header_field = NULL;
	settings->on_header_value = NULL;
	settings->on_headers_complete = NULL;
	settings->on_body = upipe_http_src_body_cb;
	settings->on_message_complete = NULL;

    /* parse url */
    ret = http_parser_parse_url(url, strlen(url), 0, &parsed_url);
    if (unlikely(ret != 0)) {
        return -1;
    }
    if (unlikely(!(parsed_url.field_set & (1 << UF_HOST)))) {
        return -1;
    }
    host = strndup(url + parsed_url.field_data[UF_HOST].off,
                                    parsed_url.field_data[UF_HOST].len);

    if (parsed_url.field_set & (1 <<UF_PORT)) {
        service = strndup(url + parsed_url.field_data[UF_PORT].off,
                                    parsed_url.field_data[UF_HOST].len);
    } else {
        service = strdup("http");
    }

    /* get socket information */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    ret = getaddrinfo(host, service, &hints, &info);
    if (unlikely(ret)) {
        upipe_err_va(upipe, "%s", gai_strerror(ret));
        free(host);
        free(service);
        return -1;
    }
    free(host);
    free(service);

    /* connect to first working ressource */
    for (res = info; res; res = res->ai_next) {
        fd = socket(res->ai_family, res->ai_socktype,
                                          res->ai_protocol);
        if (likely(fd > 0)) {
            if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
                break;
            }
            close(fd);
            fd = -1;
        }
    }
    if (fd < 0) {
        upipe_err(upipe, "could not connect to any ressource");
        freeaddrinfo(info);
        return -1;
    }
    freeaddrinfo(info);

    return fd;
}

/** @internal @This builds and sends a GET request
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_http_src_send_request(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    const char *url = upipe_http_src->url;
    int len, ret;
    char req[strlen(get_request_format)+strlen(url)+strlen(USER_AGENT)+1];

    /* build get request */
    len = snprintf(req, sizeof(req), get_request_format, url, USER_AGENT); 

    ret = send(upipe_http_src->fd, req, len, 0);

    if (ret < 0) {
        switch(errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* try again later */
                return false;

            case EBADF:
            case EINVAL:
            default:
                upipe_err_va(upipe, "error sending request (%s)", strerror(errno));
                return false;
        }
    }
    
    return true;
}

/** @internal @This asks to open the given http.
 *
 * @param upipe description structure of the pipe
 * @param url relative or absolute url of the http
 * @return false in case of error
 */
static bool upipe_http_src_set_uri(struct upipe *upipe, const char *url)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (unlikely(upipe_http_src->fd != -1)) {
        if (likely(upipe_http_src->url != NULL))
            upipe_notice_va(upipe, "closing %s", upipe_http_src->url);
        close(upipe_http_src->fd);
        upipe_http_src->fd = -1;
    }
    free(upipe_http_src->url);
    upipe_http_src->url = NULL;
    upipe_http_src_set_upump_read(upipe, NULL);

    if (unlikely(url == NULL))
        return true;

    if (upipe_http_src->uref_mgr == NULL) {
        upipe_throw_need_uref_mgr(upipe);
        if (unlikely(upipe_http_src->uref_mgr == NULL))
            return false;
    }
    if (upipe_http_src->flow_def == NULL) {
        struct uref *flow_def = uref_block_flow_alloc_def(upipe_http_src->uref_mgr,
                                                          NULL);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return false;
        }
        upipe_http_src_store_flow_def(upipe, flow_def);
    }
    if (upipe_http_src->upump_mgr == NULL)
        upipe_throw_need_upump_mgr(upipe);
    if (upipe_http_src->ubuf_mgr == NULL) {
        upipe_throw_need_ubuf_mgr(upipe, upipe_http_src->flow_def);
        if (unlikely(upipe_http_src->ubuf_mgr == NULL))
            return false;
    }

    /* now call real code */
    upipe_http_src->fd = upipe_http_src_open_url(upipe ,url);
    if (unlikely(upipe_http_src->fd < 0)) {
        upipe_err_va(upipe, "can't open url %s", url);
        return false;
    }

    /* keep url in memory */
    upipe_http_src->url = strdup(url);
    if (unlikely(upipe_http_src->url == NULL)) {
        close(upipe_http_src->fd);
        upipe_http_src->fd = -1;
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return false;
    }
    upipe_notice_va(upipe, "opening url %s", upipe_http_src->url);

    if (unlikely(!upipe_http_src_send_request(upipe))) {
        /* FIXME: build write pump */
        close(upipe_http_src->fd);
        upipe_http_src->fd = -1;
        return false;
    }
    return true;
}

/** @internal @This processes control commands on a http source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_http_src_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_http_src_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_http_src_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_http_src_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_http_src_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_http_src_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_http_src_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_http_src_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_http_src_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            upipe_http_src_set_upump_read(upipe, NULL);
            return upipe_http_src_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_http_src_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            upipe_http_src_set_upump_read(upipe, NULL);
            return upipe_http_src_set_uclock(upipe, uclock);
        }
        case UPIPE_SOURCE_GET_READ_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_http_src_get_read_size(upipe, p);
        }
        case UPIPE_SOURCE_SET_READ_SIZE: {
            unsigned int read_size = va_arg(args, unsigned int);
            return upipe_http_src_set_read_size(upipe, read_size);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_http_src_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_http_src_set_uri(upipe, uri);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a http source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_http_src_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    if (unlikely(!_upipe_http_src_control(upipe, command, args)))
        return false;

    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    if (upipe_http_src->upump_mgr != NULL && upipe_http_src->fd != -1 &&
        upipe_http_src->upump_read == NULL) {

        struct upump *upump = upump_alloc_fd_read(upipe_http_src->upump_mgr,
                                                  upipe_http_src_worker, upipe,
                                                  upipe_http_src->fd);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return false;
        }
        upipe_http_src_set_upump_read(upipe, upump);
        upump_start(upump);
    }

    return true;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_http_src_free(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    if (likely(upipe_http_src->fd != -1)) {
        if (likely(upipe_http_src->url != NULL))
            upipe_notice_va(upipe, "closing %s", upipe_http_src->url);
        close(upipe_http_src->fd);
    }
    upipe_throw_dead(upipe);

    free(upipe_http_src->url);
    upipe_http_src_clean_read_size(upipe);
    upipe_http_src_clean_uclock(upipe);
    upipe_http_src_clean_upump_read(upipe);
    upipe_http_src_clean_upump_mgr(upipe);
    upipe_http_src_clean_output(upipe);
    upipe_http_src_clean_ubuf_mgr(upipe);
    upipe_http_src_clean_uref_mgr(upipe);
    upipe_http_src_clean_urefcount(upipe);
    upipe_http_src_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_http_src_mgr = {
    .refcount = NULL,
    .signature = UPIPE_HTTP_SRC_SIGNATURE,

    .upipe_alloc = upipe_http_src_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_http_src_control
};

/** @This returns the management structure for all http source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_http_src_mgr_alloc(void)
{
    return &upipe_http_src_mgr;
}

/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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

#include <stdio.h>
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
#define HTTP_VERSION            "HTTP/1.0"
#define USER_AGENT              "upipe_http_src"

struct http_range {
    uint64_t offset;
    uint64_t length;
};

#define HTTP_RANGE(Offset, Length)      \
    (struct http_range){ .offset = Offset, .length = Length }

/** @hidden */
static int upipe_http_src_check(struct upipe *upipe, struct uref *flow_format);

struct header {
    const char *value;
    size_t len;
};

#define HEADER(Value, Len) \
    (struct header){ .value = Value, .len = Len }

/** @internal @This is the private context of a http source pipe. */
struct upipe_http_src {
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
    /** write watcher */
    struct upump *upump_write;

    /** socket descriptor */
    int fd;
    /** a request is pending */
    bool request_pending;
    /** http url */
    char *url;
    /** host */
    char *host;
    /** path part of the url */
    const char *path;

    struct header header_field;

    /** header location for 302 location */
    char *location;

    /** range */
    struct http_range range;
    uint64_t position;

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

UPIPE_HELPER_OUTPUT(upipe_http_src, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_http_src, uref_mgr, uref_mgr_request,
                      upipe_http_src_check,
                      upipe_http_src_register_output_request,
                      upipe_http_src_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_http_src, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_http_src_check,
                      upipe_http_src_register_output_request,
                      upipe_http_src_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_http_src, uclock, uclock_request, upipe_http_src_check,
                    upipe_http_src_register_output_request,
                    upipe_http_src_unregister_output_request)

UPIPE_HELPER_UPUMP_MGR(upipe_http_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump, upump_mgr)
UPIPE_HELPER_OUTPUT_SIZE(upipe_http_src, output_size)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_write, upump_mgr)

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
    upipe_http_src_init_urefcount(upipe);
    upipe_http_src_init_uref_mgr(upipe);
    upipe_http_src_init_ubuf_mgr(upipe);
    upipe_http_src_init_output(upipe);
    upipe_http_src_init_upump_mgr(upipe);
    upipe_http_src_init_upump(upipe);
    upipe_http_src_init_upump_write(upipe);
    upipe_http_src_init_uclock(upipe);
    upipe_http_src_init_output_size(upipe, UBUF_DEFAULT_SIZE);

    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    upipe_http_src->fd = -1;
    upipe_http_src->request_pending = false;
    upipe_http_src->url = NULL;
    upipe_http_src->range = HTTP_RANGE(0, -1);
    upipe_http_src->position = 0;
    upipe_http_src->path = NULL;
    upipe_http_src->host = NULL;
    upipe_http_src->location = NULL;
    upipe_http_src->header_field = HEADER(NULL, 0);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
struct part {
    const char *at;
    size_t len;
};

#define PART(At, Len) (struct part){ .at = At, .len = Len }
#define PART_NULL PART(NULL, 0)
#define PART_IS_NULL(Part)      ((Part).at == NULL)

static struct part part_make(const char *value, size_t len)
{
    return (struct part){ .at = value, .len = len};
}

static struct part part_while(const struct part *value,
                              const char *contains)
{
    struct part part = PART_NULL;

    for (size_t i = 0; i < value->len && value->at[i] != '\0'; i++) {
        size_t j;
        for (j = 0; contains[j]; j++)
            if (contains[j] == value->at[i])
                break;

        if (!contains[j])
            return part;

        part.at = value->at;
        part.len++;
    }

    return part;
}

static struct part part_remove_while(const struct part *value,
                                     const char *contains)
{
    struct part remove = part_while(value, contains);
    return PART(value->at + remove.len, value->len + remove.len);
}

static struct part part_until(const struct part *value,
                              const char *except)
{
    struct part part = PART_NULL;

    for (size_t i = 0; i < value->len && value->at[i] != '\0'; i++) {
        for (size_t j = 0; except[j]; j++)
            if (except[j] == value->at[i])
                return part;

        part.at = value->at;
        part.len++;
    }

    return part;
}

static struct part part_name(const struct part *value)
{
    struct part cleaned = part_remove_while(value, " ");
    struct part pair = part_until(&cleaned, ";");
    if (PART_IS_NULL(pair))
        return pair;
    return part_until(&pair, "=");
}

static int part_cmp(const struct part *a, const struct part *b)
{
    if (a->len != b->len)
        return a->len > b->len ? 1 : -1;
    return strncmp(a->at, b->at, a->len);
}

/** @internal @This retrieves the upipe_http_src structure from parser
 * @param parser http parser structure
 * @return pointer to upipe_http_src private structure
 */
static inline struct upipe_http_src *upipe_http_src_from_parser(http_parser *parser)
{
    return container_of(parser, struct upipe_http_src, parser);
}

static int upipe_http_src_header_field(http_parser *parser,
                                       const char *at,
                                       size_t len)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    upipe_http_src->header_field = HEADER(at, len);
    return 0;
}

static int upipe_http_src_header_value(http_parser *parser,
                                       const char *at,
                                       size_t len)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    struct upipe *upipe = upipe_http_src_to_upipe(upipe_http_src);

    struct header field = upipe_http_src->header_field;
    upipe_http_src->header_field = HEADER(NULL, 0);
    assert(field.value != NULL);

    upipe_verbose_va(upipe, "%.*s: %.*s", field.len, field.value, len, at);
    if (!strncasecmp("Location", field.value, field.len)) {
        upipe_http_src->location = strndup(at, len);
    }
    return 0;
}

static int upipe_http_src_status_cb(http_parser *parser)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    struct upipe *upipe = upipe_http_src_to_upipe(upipe_http_src);

    upipe_dbg_va(upipe, "reply http code %i", parser->status_code);

    switch (parser->status_code) {
    /* success */
    case 200:
    /* found */
    case 302:
        break;
    default:
        return -1;
    }
    return 0;
}

static int upipe_http_src_message_complete(http_parser *parser)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    struct upipe *upipe = upipe_http_src_to_upipe(upipe_http_src);

    switch (parser->status_code) {
    case 302:
        upipe_http_src_throw_redirect(upipe, upipe_http_src->location);
        upipe_set_uri(upipe, upipe_http_src->location);
        return 0;
    }
    return 0;
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

    upipe_verbose_va(upipe, "received %zu bytes of body", len);

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
    upipe_use(upipe);
    upipe_http_src_output(upipe, uref, &upipe_http_src->upump);
    upipe_http_src->position += len;
    upipe_release(upipe);

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
    char *buffer = malloc(upipe_http_src->output_size); /* FIXME use ubuf/umem */
    assert(buffer);
    memset(buffer, 0, upipe_http_src->output_size);

    ssize_t len = recv(upipe_http_src->fd, buffer, upipe_http_src->output_size, 0);

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
        upipe_http_src_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }
    if (unlikely(len == 0)) {
        free(buffer);
        upipe_notice_va(upipe, "end of %s", upipe_http_src->url);
        upipe_http_src_set_upump(upipe, NULL);
        upipe_throw_source_end(upipe);
        return;
    }

    /* parse response */
    size_t parsed_len =
        http_parser_execute(&upipe_http_src->parser,
                            &upipe_http_src->parser_settings,
                            buffer, len);
    if (parsed_len != len)
        upipe_warn(upipe, "http request execution failed");
    free(buffer);
}

static int request_add(char **req_p, size_t *len, const char *fmt, ...)
{
    va_list args;

    if (!*req_p)
        return -1;

    va_start(args, fmt);
    int ret = vsnprintf(*req_p, *len, fmt, args);
    va_end(args);

    if (ret < 0 || (size_t)ret >= *len) {
        *req_p = NULL;
        return -1;
    }

    *len -= ret;
    *req_p += ret;
    return 0;
}

/** @internal @This builds and sends a GET request
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_http_src_send_request(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    const char *url = upipe_http_src->path;
    char req_buffer[4096 + strlen(url)];
    char *req = req_buffer;
    size_t req_buffer_len = sizeof (req_buffer);
    size_t req_len = req_buffer_len;

    /* build get request */
    upipe_dbg_va(upipe, "GET %s", url);
    request_add(&req, &req_len, "GET %s %s\r\n", url, HTTP_VERSION);
    upipe_verbose_va(upipe, "User-Agent: %s", USER_AGENT);
    request_add(&req, &req_len, "User-Agent: %s\r\n", USER_AGENT);
    if (upipe_http_src->host) {
        upipe_verbose_va(upipe, "Host: %s", upipe_http_src->host);
        request_add(&req, &req_len, "Host: %s\r\n", upipe_http_src->host);
    }
    upipe_http_src->position = 0;
    if (upipe_http_src->range.offset ||
        upipe_http_src->range.length != (uint64_t)-1) {

        if (upipe_http_src->range.offset) {
            upipe_verbose_va(upipe, "range offset: %"PRIu64,
                             upipe_http_src->range.offset);
            request_add(&req, &req_len, "Range: bytes=%"PRIu64"-",
                        upipe_http_src->range.offset);
            upipe_http_src->position = upipe_http_src->range.offset;
        }
        else
            request_add(&req, &req_len, "Range: bytes=0-");

        if (upipe_http_src->range.length != (uint64_t)-1) {
            upipe_verbose_va(upipe, "range length: %"PRIu64,
                             upipe_http_src->range.length);
            request_add(&req, &req_len, "%"PRIu64,
                        upipe_http_src->range.offset +
                        upipe_http_src->range.length);
        }

        request_add(&req, &req_len, "\r\n");
    }
    request_add(&req, &req_len, "\r\n");

    if (unlikely(req == NULL)) {
        upipe_err_va(upipe, "request is too long: %s", req_buffer);
        return UBASE_ERR_ALLOC;
    }

    int ret = send(upipe_http_src->fd, req_buffer, req_buffer_len - req_len, 0);
    if (ret < 0) {
        switch(errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* try again later */
                return UBASE_ERR_EXTERNAL;

            case EBADF:
            case EINVAL:
            default:
                upipe_err_va(upipe, "error sending request (%s)", strerror(errno));
                return UBASE_ERR_EXTERNAL;
        }
    }

    return UBASE_ERR_NONE;
}

static void upipe_http_src_worker_write(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (unlikely(upipe_http_src_send_request(upipe)) != UBASE_ERR_NONE) {
        close(upipe_http_src->fd);
        upipe_http_src->fd = -1;
    }

    upipe_http_src->request_pending = false;
    upipe_http_src_set_upump_write(upipe, NULL);
}

/** @internal @This checks if the pump may be allocated.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_http_src_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_http_src_store_flow_def(upipe, flow_format);

    upipe_http_src_check_upump_mgr(upipe);
    if (upipe_http_src->upump_mgr == NULL)
        return UBASE_ERR_NONE;

    if (upipe_http_src->uref_mgr == NULL) {
        upipe_http_src_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    if (upipe_http_src->ubuf_mgr == NULL) {
        struct uref *flow_format =
            uref_block_flow_alloc_def(upipe_http_src->uref_mgr, NULL);
        uref_block_flow_set_size(flow_format, upipe_http_src->output_size);
        if (unlikely(flow_format == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_http_src_require_ubuf_mgr(upipe, flow_format);
        return UBASE_ERR_NONE;
    }

    if (upipe_http_src->uclock == NULL &&
        urequest_get_opaque(&upipe_http_src->uclock_request, struct upipe *)
            != NULL)
        return UBASE_ERR_NONE;

    if (upipe_http_src->fd != -1) {
        if (upipe_http_src->upump == NULL) {
            struct upump *upump;
            upump = upump_alloc_fd_read(upipe_http_src->upump_mgr,
                                        upipe_http_src_worker, upipe,
                                        upipe_http_src->fd);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump(upipe, upump);
            upump_start(upump);
        }

        if (upipe_http_src->upump_write == NULL &&
            upipe_http_src->request_pending) {
            struct upump *upump =
                upump_alloc_fd_write(upipe_http_src->upump_mgr,
                                     upipe_http_src_worker_write,
                                     upipe, upipe_http_src->fd);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump_write(upipe, upump);
            upump_start(upump);
        }
    }
    return UBASE_ERR_NONE;
}

/** @internal @This returns the url of the currently opened http.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the url of the http
 * @return an error code
 */
static int upipe_http_src_get_uri(struct upipe *upipe, const char **url_p)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    assert(url_p != NULL);
    *url_p = upipe_http_src->url;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given http (real code here).
 *
 * @param upipe description structure of the pipe
 * @param url relative or absolute url of the http
 * @return socket fd or -1 in case of error
 */
static int upipe_http_src_open_url(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    http_parser *parser = &upipe_http_src->parser;
    http_parser_settings *settings = &upipe_http_src->parser_settings;
    struct http_parser_url parsed_url;
    const char *url = upipe_http_src->url;
    struct addrinfo *info = NULL, *res;
    struct addrinfo hints;
    char *service;
    int ret, fd = -1;

    if (!url)
        return -1;

    /* check url size */
    if (unlikely(strnlen(url, MAX_URL_SIZE + 1) > MAX_URL_SIZE)) {
        upipe_err(upipe, "url too large, something's fishy");
        return -1;
    }

    /* init parser and settings */
    http_parser_init(parser, HTTP_RESPONSE);
    settings->on_message_begin = NULL;
    settings->on_url = NULL;
    settings->on_header_field = upipe_http_src_header_field;
    settings->on_header_value = upipe_http_src_header_value;
    settings->on_headers_complete = NULL;
    settings->on_body = upipe_http_src_body_cb;
    settings->on_message_complete = upipe_http_src_message_complete;
    settings->on_status_complete = upipe_http_src_status_cb;

    /* parse url */
    ret = http_parser_parse_url(url, strlen(url), 0, &parsed_url);
    if (unlikely(ret != 0)) {
        return -1;
    }
    if (unlikely(!(parsed_url.field_set & (1 << UF_HOST)))) {
        return -1;
    }
    upipe_http_src->host = strndup(url + parsed_url.field_data[UF_HOST].off,
                                   parsed_url.field_data[UF_HOST].len);

    if (parsed_url.field_set & (1 <<UF_PORT)) {
        service = strndup(url + parsed_url.field_data[UF_PORT].off,
                                    parsed_url.field_data[UF_HOST].len);
    } else {
        service = strdup("http");
    }

    if (parsed_url.field_set & (1 << UF_PATH)) {
        upipe_http_src->path = url + parsed_url.field_data[UF_PATH].off;
    }
    else {
        upipe_http_src->path = "/";
    }

    /* get socket information */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    ret = getaddrinfo(upipe_http_src->host, service, &hints, &info);
    if (unlikely(ret)) {
        upipe_err_va(upipe, "%s", gai_strerror(ret));
        free(service);
        return -1;
    }
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

/** @internal @This asks to open the given http.
 *
 * @param upipe description structure of the pipe
 * @param url relative or absolute url of the http
 * @return an error code
 */
static int upipe_http_src_set_uri(struct upipe *upipe, const char *url)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (unlikely(upipe_http_src->fd != -1)) {
        if (likely(upipe_http_src->url != NULL))
            upipe_notice_va(upipe, "closing %s", upipe_http_src->url);
        close(upipe_http_src->fd);
        upipe_http_src->fd = -1;
    }
    upipe_http_src->path = NULL;
    free(upipe_http_src->host);
    upipe_http_src->host = NULL;
    free(upipe_http_src->url);
    upipe_http_src->url = NULL;
    upipe_http_src_set_upump(upipe, NULL);
    upipe_http_src->request_pending = false;
    upipe_http_src_set_upump_write(upipe, NULL);

    if (unlikely(url == NULL))
        return UBASE_ERR_NONE;

    upipe_http_src->url = strdup(url);
    if (unlikely(upipe_http_src->url == NULL))
        return UBASE_ERR_ALLOC;

    /* now call real code */
    upipe_http_src->fd = upipe_http_src_open_url(upipe);
    if (unlikely(upipe_http_src->fd < 0)) {
        upipe_err_va(upipe, "can't open url %s", url);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_notice_va(upipe, "opening url %s", upipe_http_src->url);
    upipe_http_src->request_pending = true;
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_get_position(struct upipe *upipe,
                                        uint64_t *position_p)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (position_p)
        *position_p = upipe_http_src->position;
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_set_position(struct upipe *upipe,
                                        uint64_t offset)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    upipe_http_src->range = HTTP_RANGE(offset, 0);
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_set_range(struct upipe *upipe,
                                     uint64_t offset,
                                     uint64_t length)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    upipe_http_src->range = HTTP_RANGE(offset, length);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a http source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_http_src_control(struct upipe *upipe,
                                   int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_http_src_set_upump(upipe, NULL);
            return upipe_http_src_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_http_src_set_upump(upipe, NULL);
            upipe_http_src_require_uclock(upipe);
            return UBASE_ERR_NONE;

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

        case UPIPE_GET_OUTPUT_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_http_src_get_output_size(upipe, p);
        }
        case UPIPE_SET_OUTPUT_SIZE: {
            unsigned int output_size = va_arg(args, unsigned int);
            return upipe_http_src_set_output_size(upipe, output_size);
        }

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_http_src_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_http_src_set_uri(upipe, uri);
        }

        case UPIPE_HTTP_SRC_GET_POSITION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            uint64_t *position_p = va_arg(args, uint64_t *);
            return _upipe_http_src_get_position(upipe, position_p);
        }
        case UPIPE_HTTP_SRC_SET_POSITION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            uint64_t offset = va_arg(args, uint64_t);
            return _upipe_http_src_set_position(upipe, offset);
        }

        case UPIPE_HTTP_SRC_SET_RANGE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            uint64_t offset = va_arg(args, uint64_t);
            uint64_t length = va_arg(args, uint64_t);
            return _upipe_http_src_set_range(upipe, offset, length);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a http source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_http_src_control(struct upipe *upipe,
                                  int command, va_list args)
{
    UBASE_RETURN(_upipe_http_src_control(upipe, command, args));

    return upipe_http_src_check(upipe, NULL);
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

    free(upipe_http_src->host);
    free(upipe_http_src->url);
    upipe_http_src_clean_output_size(upipe);
    upipe_http_src_clean_uclock(upipe);
    upipe_http_src_clean_upump_write(upipe);
    upipe_http_src_clean_upump(upipe);
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
    .upipe_control = upipe_http_src_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all http source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_http_src_mgr_alloc(void)
{
    return &upipe_http_src_mgr;
}

/*
 * Copyright (C) 2013-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Arnaud de Turckheim
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

#include "upipe/ubase.h"
#include "upipe/ucookie.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_uri.h"
#include "upipe/upump.h"
#include "upipe/ueventfd.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe/upipe_helper_output_size.h"
#include "upipe-modules/upipe_http_source.h"
#include "upipe-modules/uref_http_flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>

#include "http_source_hook.h"
#include "http-parser/http_parser.h"

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096

#define MAX_URL_SIZE            2048
#define HTTP_VERSION            "HTTP/1.1"
#define USER_AGENT              "upipe_http_src/1.0"
#define TIMEOUT                 (5 * 27000000) /* 5s */

struct http_range {
    uint64_t offset;
    uint64_t length;
};

#define HTTP_RANGE(Offset, Length)      \
    (struct http_range){ .offset = Offset, .length = Length }

struct upipe_http_src_cookie {
    struct uchain uchain;
    char *value;
    struct ucookie ucookie;
};

UBASE_FROM_TO(upipe_http_src_cookie, uchain, uchain, uchain)

/** @hidden */
static int upipe_http_src_check(struct upipe *upipe, struct uref *flow_format);

struct header {
    const char *value;
    size_t len;
};

#define UPIPE_HTTP_SRC_REQUEST_BLOCK_SIZE   16384

/** @internal @This stores a pending request */
struct upipe_http_src_request {
    /** pending request buffer */
    char *buf;
    /** pending request length */
    size_t len;
    /** pending request buffer size */
    size_t size;
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

    /** data input ready */
    struct ueventfd data_in;
    /** data output ready */
    struct ueventfd data_out;

    /** read size */
    unsigned int output_size;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump_read;
    /** write watcher */
    struct upump *upump_write;
    /** timeout watcher */
    struct upump *upump_timeout;
    /** write watcher */
    struct upump *upump_data_in;
    /** read watcher */
    struct upump *upump_data_out;

    /** socket descriptor */
    int fd;
    /** pending request */
    struct upipe_http_src_request request;
    /** http url */
    char *url;

    struct header header_field;

    /** header location for 302 location */
    char *location;
    /** http proxy */
    char *proxy;
    /** user agent */
    char *user_agent;

    /** range */
    struct http_range range;
    uint64_t position;

    /** http parser*/
    http_parser parser;

    /** http parser settings */
    http_parser_settings parser_settings;

    /** read/write timeout value */
    uint64_t timeout;

    /** default plain hook */
    struct http_src_hook http_hook;

    /** read/write hook */
    struct upipe_http_src_hook *hook;

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

UPIPE_HELPER_OUTPUT_SIZE(upipe_http_src, output_size)
UPIPE_HELPER_UPUMP_MGR(upipe_http_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_read, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_write, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_timeout, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_data_in, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_http_src, upump_data_out, upump_mgr)

static int upipe_http_src_header_field(http_parser *parser,
                                       const char *at,
                                       size_t len);
static int upipe_http_src_header_value(http_parser *parser,
                                       const char *at,
                                       size_t len);
static int upipe_http_src_body_cb(http_parser *parser,
                                  const char *at,
                                  size_t len);
static int upipe_http_src_message_complete(http_parser *parser);
static int upipe_http_src_status_cb(http_parser *parser);

/** @This throw a scheme hook event.
 *
 * @param upipe description structure of the pipe
 * @param flow_def uri attributes
 * @param hook read/write hook to be overwrite
 * @return an error code
 */
static int upipe_http_src_throw_scheme_hook(struct upipe *upipe,
                                            struct uref *flow_def,
                                            struct upipe_http_src_hook **hook)
{
    const char *scheme = "(none)";
    uref_uri_get_scheme(flow_def, &scheme);
    upipe_verbose_va(upipe, "throw scheme hook for %s", scheme);
    return upipe_throw(upipe, UPROBE_HTTP_SRC_SCHEME_HOOK,
                       UPIPE_HTTP_SRC_SIGNATURE, flow_def, hook);
}

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
    upipe_http_src_init_upump_read(upipe);
    upipe_http_src_init_upump_write(upipe);
    upipe_http_src_init_upump_timeout(upipe);
    upipe_http_src_init_upump_data_in(upipe);
    upipe_http_src_init_upump_data_out(upipe);
    upipe_http_src_init_uclock(upipe);
    upipe_http_src_init_output_size(upipe, UBUF_DEFAULT_SIZE);

    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    upipe_http_src->fd = -1;
    upipe_http_src->url = NULL;
    upipe_http_src->range = HTTP_RANGE(0, -1);
    upipe_http_src->position = 0;
    upipe_http_src->location = NULL;
    upipe_http_src->header_field = HEADER(NULL, 0);
    upipe_http_src->proxy = NULL;
    upipe_http_src->user_agent = NULL;
    upipe_http_src->timeout = TIMEOUT;
    upipe_http_src->request.buf = NULL;
    upipe_http_src->request.len = 0;
    upipe_http_src->request.size = 0;
    upipe_http_src->hook = NULL;
    ueventfd_init(&upipe_http_src->data_in, false);
    ueventfd_init(&upipe_http_src->data_out, false);

    /* init parser settings */
    http_parser_settings *settings = &upipe_http_src->parser_settings;
    settings->on_message_begin = NULL;
    settings->on_url = NULL;
    settings->on_header_field = upipe_http_src_header_field;
    settings->on_header_value = upipe_http_src_header_value;
    settings->on_headers_complete = NULL;
    settings->on_body = upipe_http_src_body_cb;
    settings->on_message_complete = upipe_http_src_message_complete;
    settings->on_status_complete = upipe_http_src_status_cb;

    upipe_throw_ready(upipe);

    const char *proxy;
    if (!ubase_check(upipe_http_src_mgr_get_proxy(mgr, &proxy))) {
        upipe_warn(upipe, "fail to get http proxy from manager");
        proxy = NULL;
    }

    if (proxy && !ubase_check(upipe_http_src_set_proxy(upipe, proxy)))
        upipe_warn(upipe, "fail to set http proxy");

    const char *user_agent = NULL;
    upipe_http_src_mgr_get_user_agent(mgr, &user_agent);
    if (!ubase_check(upipe_http_src_set_user_agent(upipe, user_agent)))
        upipe_warn(upipe, "fail to set user agent");

    return upipe;
}

/** @This closes a connection.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_http_src_close(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    struct uref *flow_def = upipe_http_src->flow_def;

    if (likely(upipe_http_src->url != NULL))
        upipe_notice_va(upipe, "closing %s", upipe_http_src->url);
    upipe_http_src_hook_release(upipe_http_src->hook);
    upipe_http_src->hook = NULL;
    ubase_clean_fd(&upipe_http_src->fd);
    ubase_clean_str(&upipe_http_src->url);
    free(upipe_http_src->request.buf);
    upipe_http_src->request.buf = NULL;
    upipe_http_src->request.len = 0;
    upipe_http_src->request.size = 0;
    upipe_http_src_set_upump_read(upipe, NULL);
    upipe_http_src_set_upump_write(upipe, NULL);
    upipe_http_src_set_upump_timeout(upipe, NULL);
    upipe_http_src_set_upump_data_in(upipe, NULL);
    upipe_http_src_set_upump_data_out(upipe, NULL);
    if (flow_def)
        uref_http_delete_content_type(flow_def);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_http_src_free(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    upipe_http_src_close(upipe);

    upipe_throw_dead(upipe);

    ueventfd_clean(&upipe_http_src->data_in);
    ueventfd_clean(&upipe_http_src->data_out);
    free(upipe_http_src->user_agent);
    free(upipe_http_src->proxy);
    free(upipe_http_src->url);
    free(upipe_http_src->location);
    upipe_http_src_clean_output_size(upipe);
    upipe_http_src_clean_uclock(upipe);
    upipe_http_src_clean_upump_data_out(upipe);
    upipe_http_src_clean_upump_data_in(upipe);
    upipe_http_src_clean_upump_timeout(upipe);
    upipe_http_src_clean_upump_write(upipe);
    upipe_http_src_clean_upump_read(upipe);
    upipe_http_src_clean_upump_mgr(upipe);
    upipe_http_src_clean_output(upipe);
    upipe_http_src_clean_ubuf_mgr(upipe);
    upipe_http_src_clean_uref_mgr(upipe);
    upipe_http_src_clean_urefcount(upipe);
    upipe_http_src_free_void(upipe);
}

static int upipe_http_src_add_cookie(struct upipe *upipe,
                                      const char *buf,
                                      size_t len)
{
    char cookie[len + 1];
    memcpy(cookie, buf, len);
    cookie[len] = '\0';
    upipe_dbg_va(upipe, "add cookie %s", cookie);
    return upipe_http_src_mgr_set_cookie(upipe->mgr, cookie);
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
    struct uref *flow_def = upipe_http_src->flow_def;

    struct header field = upipe_http_src->header_field;
    upipe_http_src->header_field = HEADER(NULL, 0);
    assert(field.value != NULL);

    upipe_verbose_va(upipe, "%.*s: %.*s",
                     (int)field.len, field.value,
                     (int)len, at);
    if (!strncasecmp("Location", field.value, field.len)) {
        upipe_http_src->location = strndup(at, len);
    }
    else if (!strncasecmp("Set-Cookie", field.value, field.len)) {
        if (!ubase_check(upipe_http_src_add_cookie(upipe, at, len)))
            upipe_warn_va(upipe, "fail to set cookie %.*s", (int)len, at);
    }
    else if (!strncasecmp("Content-Type", field.value, field.len)) {
        char content_type[len + 1];
        snprintf(content_type, len + 1, "%.*s", (int)len, at);
        uref_http_set_content_type(flow_def, content_type);
    }
    return 0;
}

static int upipe_http_src_status_cb(http_parser *parser)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    struct upipe *upipe = upipe_http_src_to_upipe(upipe_http_src);

    upipe_dbg_va(upipe, "reply http code %i", parser->status_code);

    if (parser->status_code < 400)
        return 0;
    upipe_http_src_throw_error(upipe, parser->status_code);
    return -1;
}

static int upipe_http_src_output_data(struct upipe *upipe,
                                      const char *at, size_t len)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    struct uref *uref;
    uint64_t systime = 0;
    uint8_t *buf = NULL;
    int size;

    if (unlikely(at == NULL))
        len = 0;

    /* fetch systime */
    if (likely(upipe_http_src->uclock)) {
        systime = uclock_now(upipe_http_src->uclock);
    }

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
    if (likely(at != NULL))
        memcpy(buf, at, len);
    uref_block_unmap(uref, 0);

    if (systime)
        uref_clock_set_cr_sys(uref, systime);
    if (len == 0)
        uref_block_set_end(uref);
    upipe_http_src->position += len;
    upipe_http_src_output(upipe, uref, &upipe_http_src->upump_read);

    /* everything's fine, return 0 to http_parser */
    return 0;
}

/** @internal @This is called by http_parser when message is completed.
 *
 * @param parser http parser structure
 * @return 0
 */
static int upipe_http_src_message_complete(http_parser *parser)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_parser(parser);
    struct upipe *upipe = upipe_http_src_to_upipe(upipe_http_src);
    char *location = upipe_http_src->location;
    int status_code = parser->status_code;

    upipe_http_src->location = NULL;

    upipe_dbg_va(upipe, "message complete %i", status_code);

    switch (status_code) {
    /* success */
    case 200:
    /* partial content */
    case 206:
        upipe_http_src_output_data(upipe, NULL, 0);
        break;
    }
    upipe_http_src_close(upipe);
    upipe_throw_source_end(upipe);

    if (status_code >= 300 && status_code < 400 && location != NULL)
        /* redirect */
        upipe_http_src_throw_redirect(upipe, location);

    free(location);

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

    upipe_verbose_va(upipe, "received %zu bytes of body", len);

    switch (parser->status_code) {
    /* success */
    case 200:
    /* partial content */
    case 206:
        upipe_http_src_output_data(upipe, at, len);
        break;
    /* redirect */
    case 302:
        break;
    }
    /* everything's fine, return 0 to http_parser */
    return 0;
}

/** @internal @This parses and outputs data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_http_src_process(struct upipe *upipe, const uint8_t *buffer,
                                   size_t size)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (size == 0)
        upipe_http_src_output_data(upipe, NULL, 0);
    else {
        size_t parsed_len =
            http_parser_execute(&upipe_http_src->parser,
                                &upipe_http_src->parser_settings,
                                (const char *)buffer, size);
        if (parsed_len != size) {
            upipe_warn(upipe, "http request execution failed");
            upipe_http_src_output_data(upipe, NULL, 0);
            upipe_http_src_close(upipe);
            upipe_throw_source_end(upipe);
        }
    }
}

UBASE_FMT_PRINTF(2, 3)
static int upipe_http_src_request_add(struct upipe *upipe, const char *fmt, ...)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    struct upipe_http_src_request *request = &upipe_http_src->request;
    int ret;

    va_list args;
    va_start(args, fmt);
    ret = vsnprintf(request->buf + request->len,
                    request->size - request->len,
                    fmt, args);
    va_end(args);

    if (ret < 0)
        return UBASE_ERR_INVALID;

    if (request->len + ret >= request->size) {
        /* reallocate the request buffer */
        size_t new_size = request->size + UPIPE_HTTP_SRC_REQUEST_BLOCK_SIZE;
        if (request->len + ret >= new_size)
            new_size = request->len + ret + 1;
        char *buf = realloc(request->buf, new_size);
        if (!buf)
            return UBASE_ERR_ALLOC;

        request->buf = buf;
        request->size = new_size;

        va_start(args, fmt);
        ret = vsnprintf(request->buf + request->len,
                        request->size - request->len,
                        fmt, args);
        va_end(args);

        if (ret < 0)
            return UBASE_ERR_INVALID;
    }

    request->len += ret;

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
    struct uref *flow_def = upipe_http_src->flow_def;
    int ret;

    const char *path;
    ret = uref_uri_get_path(flow_def, &path);
    if (!ubase_check(ret)) {
        upipe_err(upipe, "fail to get path");
        return ret;
    }
    if (!strlen(path))
        path = "/";

    const char *query;
    if (!ubase_check(uref_uri_get_query(flow_def, &query)))
        query = NULL;

    /* GET url */
    if (upipe_http_src->proxy) {
        upipe_dbg_va(upipe, "GET %s", upipe_http_src->url);
        upipe_http_src_request_add(upipe, "GET %s %s\r\n",
                                   upipe_http_src->url, HTTP_VERSION);
    }
    else {
        char url[strlen(path) + 1 + (query ? strlen(query) : 0) + 1];
        sprintf(url, "%s%s%s", path, query ? "?" : "", query ? query : "");

        upipe_dbg_va(upipe, "GET %s", url);
        upipe_http_src_request_add(upipe, "GET %s %s\r\n", url, HTTP_VERSION);
    }

    /* User-Agent */
    const char *user_agent = upipe_http_src->user_agent;
    if (user_agent) {
        upipe_verbose_va(upipe, "User-Agent: %s", user_agent);
        upipe_http_src_request_add(upipe, "User-Agent: %s\r\n", user_agent);
    }

    /* Host */
    const char *host = NULL;
    if (ubase_check(uref_uri_get_host(flow_def, &host))) {
        upipe_verbose_va(upipe, "Host: %s", host);
        upipe_http_src_request_add(upipe, "Host: %s\r\n", host);
    }

    /* Range */
    upipe_http_src->position = 0;
    if (upipe_http_src->range.offset ||
        upipe_http_src->range.length != (uint64_t)-1) {

        if (upipe_http_src->range.offset) {
            upipe_verbose_va(upipe, "range offset: %"PRIu64,
                             upipe_http_src->range.offset);
            upipe_http_src_request_add(upipe, "Range: bytes=%"PRIu64"-",
                                       upipe_http_src->range.offset);
            upipe_http_src->position = upipe_http_src->range.offset;
        }
        else
            upipe_http_src_request_add(upipe, "Range: bytes=0-");

        if (upipe_http_src->range.length != (uint64_t)-1) {
            upipe_verbose_va(upipe, "range length: %"PRIu64,
                             upipe_http_src->range.length);
            upipe_http_src_request_add(upipe, "%"PRIu64,
                                       upipe_http_src->range.offset +
                                       upipe_http_src->range.length);
        }

        upipe_http_src_request_add(upipe, "\r\n");
    }

    /* Cookie */
    struct uchain *uchain = NULL;
    bool first = true;
    while (ubase_check(upipe_http_src_mgr_iterate_cookie(upipe->mgr,
                                                         host, path,
                                                         &uchain)) &&
           uchain != NULL) {
        struct upipe_http_src_cookie *cookie =
            upipe_http_src_cookie_from_uchain(uchain);
        upipe_verbose_va(upipe, "Cookie: %.*s=%.*s",
                    (int)cookie->ucookie.name.len, cookie->ucookie.name.at,
                    (int)cookie->ucookie.value.len, cookie->ucookie.value.at);
        if (first)
            upipe_http_src_request_add(
                upipe, "Cookie: %.*s=%.*s",
                (int)cookie->ucookie.name.len, cookie->ucookie.name.at,
                (int)cookie->ucookie.value.len, cookie->ucookie.value.at);
        else
            upipe_http_src_request_add(
                upipe, "; %.*s=%.*s",
                (int)cookie->ucookie.name.len, cookie->ucookie.name.at,
                (int)cookie->ucookie.value.len, cookie->ucookie.value.at);
        first = false;
    }
    if (!first)
        upipe_http_src_request_add(upipe, "\r\n");

    /* End of request */
    upipe_http_src_request_add(upipe, "\r\n");

    ueventfd_write(&upipe_http_src->data_in);

    return UBASE_ERR_NONE;
}

static void upipe_http_src_worker_update_state(struct upipe *upipe, int ret)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (upipe_http_src->upump_write)
        upump_stop(upipe_http_src->upump_write);
    if (upipe_http_src->upump_read)
        upump_stop(upipe_http_src->upump_read);

    if (ret & UPIPE_HTTP_SRC_HOOK_TRANSPORT_READ)
        if (upipe_http_src->upump_read)
            upump_start(upipe_http_src->upump_read);
    if (ret & UPIPE_HTTP_SRC_HOOK_TRANSPORT_WRITE)
        if (upipe_http_src->upump_write)
            upump_start(upipe_http_src->upump_write);
    if (ret & UPIPE_HTTP_SRC_HOOK_DATA_WRITE)
        ueventfd_write(&upipe_http_src->data_in);
    if (ret & UPIPE_HTTP_SRC_HOOK_DATA_READ)
        ueventfd_write(&upipe_http_src->data_out);
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the http descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_http_src_worker_read(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (likely(upipe_http_src->upump_timeout))
        upump_restart(upipe_http_src->upump_timeout);

    int ret = upipe_http_src->hook->transport.read(
        upipe, upipe_http_src->hook, upipe_http_src->fd);
    upipe_http_src_worker_update_state(upipe, ret);
}

static void upipe_http_src_worker_write(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    if (likely(upipe_http_src->upump_timeout))
        upump_restart(upipe_http_src->upump_timeout);

    int ret = upipe_http_src->hook->transport.write(
        upipe, upipe_http_src->hook, upipe_http_src->fd);
    upipe_http_src_worker_update_state(upipe, ret);
}

/** @internal @This is triggered when the connection timeout.
 *
 * @param upump description structure of the timeout
 */
static void upipe_http_src_worker_timeout(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_warn(upipe, "timeout");
    upipe_http_src_output_data(upipe, NULL, 0);
    upipe_http_src_close(upipe);
    upipe_throw_source_end(upipe);
}

/** @internal @This is triggered when data can be written.
 *
 * @param upump description structure of the watcher
 */
static void upipe_http_src_data_in(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    struct upipe_http_src_request *request = &upipe_http_src->request;

    ueventfd_read(&upipe_http_src->data_in);

    if (!request->len)
        return;

    int ret = upipe_http_src->hook->data.write(
        upipe, upipe_http_src->hook, (unsigned char *)request->buf,
        request->len);

    if (ret < 0) {
        switch(errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* try again later */
                return;

            case EBADF:
            case EINVAL:
            default:
                upipe_err_va(upipe, "error sending request (%s)",
                             strerror(errno));
                return;
        }
    }
    else if (ret == 0) {
        upipe_notice(upipe, "connection closed");
        upipe_http_src_set_upump_write(upipe, NULL);
        upipe_http_src_set_upump_timeout(upipe, NULL);
    }
    else {
        request->len -= ret;
        if (request->len) {
            memmove(request->buf, request->buf + ret, request->len);
            upump_start(upipe_http_src->upump_write);
        }
        else {
            free(request->buf);
            request->buf = NULL;
            request->size = 0;
        }

        if (upipe_http_src->upump_write)
            upump_start(upipe_http_src->upump_write);
    }
}

/** @internal @This is triggered when data can be read.
 *
 * @param upump description structure of the watcher
 */
static void upipe_http_src_data_out(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    ueventfd_read(&upipe_http_src->data_out);

    uint8_t buffer[upipe_http_src->output_size];
    ssize_t len =
        upipe_http_src->hook->data.read(
            upipe, upipe_http_src->hook, buffer, upipe_http_src->output_size);
    if (unlikely(len < 0)) {
        switch (errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* not an issue, try again later */
                return;

            default:
                break;
        }
        upipe_err_va(upipe, "read error from %s (%s)", upipe_http_src->url,
                     strerror(errno));
    }
    else if (len == 0) {
        upipe_dbg(upipe, "connection closed");
    }
    else if (len > 0)
        ueventfd_write(&upipe_http_src->data_out);

    upipe_http_src_process(upipe, buffer, len > 0 ? len : 0);

    if (len <= 0) {
        upipe_http_src_set_upump_read(upipe, NULL);
        upipe_http_src_set_upump_write(upipe, NULL);
        upipe_http_src_set_upump_timeout(upipe, NULL);
        upipe_http_src_set_upump_data_in(upipe, NULL);
        upipe_http_src_set_upump_data_out(upipe, NULL);
        upipe_throw_source_end(upipe);
    }

    if (upipe_http_src->upump_read)
        upump_start(upipe_http_src->upump_read);
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
    if (flow_format != NULL) {
        if (!upipe_http_src->flow_def)
            upipe_http_src_store_flow_def(upipe, flow_format);
        else {
            int ret = uref_flow_cmp_def(upipe_http_src->flow_def, flow_format);
            uref_free(flow_format);
            if (!ubase_check(ret))
                return ret;
        }
    }

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
        if (upipe_http_src->upump_read == NULL) {
            struct upump *upump;
            upump = upump_alloc_fd_read(upipe_http_src->upump_mgr,
                                        upipe_http_src_worker_read, upipe,
                                        upipe->refcount, upipe_http_src->fd);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump_read(upipe, upump);
            upump_start(upump);
        }

        if (upipe_http_src->upump_write == NULL) {
            struct upump *upump =
                upump_alloc_fd_write(upipe_http_src->upump_mgr,
                                     upipe_http_src_worker_write, upipe,
                                     upipe->refcount, upipe_http_src->fd);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump_write(upipe, upump);
            upump_start(upump);
        }

        if (upipe_http_src->upump_timeout == NULL) {
            struct upump *upump =
                upump_alloc_timer(upipe_http_src->upump_mgr,
                                  upipe_http_src_worker_timeout, upipe,
                                  upipe->refcount,
                                  upipe_http_src->timeout, 0);
            if (unlikely(upump == NULL)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump_timeout(upipe, upump);
            upump_start(upump);
        }

        if (!upipe_http_src->upump_data_in) {
            struct upump *upump = ueventfd_upump_alloc(
                &upipe_http_src->data_in,
                upipe_http_src->upump_mgr,
                upipe_http_src_data_in, upipe,
                upipe->refcount);
            if (unlikely(!upump)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump_data_in(upipe, upump);
            upump_start(upump);
        }

        if (!upipe_http_src->upump_data_out) {
            struct upump *upump = ueventfd_upump_alloc(
                &upipe_http_src->data_out,
                upipe_http_src->upump_mgr,
                upipe_http_src_data_out, upipe,
                upipe->refcount);
            if (unlikely(!upump)) {
                upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
                return UBASE_ERR_UPUMP;
            }
            upipe_http_src_set_upump_data_out(upipe, upump);
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
 * @return an error code
 */
static int upipe_http_src_open_url(struct upipe *upipe)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    struct uref *flow_def = upipe_http_src->flow_def;
    struct addrinfo *info = NULL, *res;
    struct addrinfo hints;
    int ret, fd = -1;

    if (unlikely(flow_def == NULL))
        return UBASE_ERR_INVALID;

    /* init parser */
    http_parser_init(&upipe_http_src->parser, HTTP_RESPONSE);

    /* get socket information */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;

    if (upipe_http_src->proxy) {
        struct uuri uuri;
        ret = uuri_from_str(&uuri, upipe_http_src->proxy);
        if (!ubase_check(ret)) {
            upipe_err_va(upipe, "invalid http_proxy %s",
                         upipe_http_src->proxy);
            return UBASE_ERR_INVALID;
        }
        char host[uuri.authority.host.len + 1];
        ustring_cpy(uuri.authority.host, host, sizeof (host));
        char service[uuri.authority.port.len + 1];
        ustring_cpy(uuri.authority.port, service, sizeof (service));

        upipe_verbose_va(upipe, "getaddrinfo to %s%s%s",
                         host, strlen(service) ? ":" : "",
                         service);
        ret = getaddrinfo(host, service, &hints, &info);
    }
    else {
        const char *host;
        UBASE_RETURN(uref_uri_get_host(flow_def, &host));

        const char *service;
        if (!ubase_check(uref_uri_get_port(flow_def, &service)))
            UBASE_RETURN(uref_uri_get_scheme(flow_def, &service));

        upipe_verbose_va(upipe, "getaddrinfo to %s", host);
        ret = getaddrinfo(host, service, &hints, &info);
    }

    if (unlikely(ret)) {
        upipe_err_va(upipe, "getaddrinfo: %s", gai_strerror(ret));
        return UBASE_ERR_EXTERNAL;
    }

    /* connect to first working resource */
    for (res = info; res; res = res->ai_next) {
        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (likely(fd >= 0)) {
            if (connect(fd, res->ai_addr, res->ai_addrlen) == 0)
                break;
            ubase_clean_fd(&fd);
        }
    }
    freeaddrinfo(info);

    if (fd < 0) {
        upipe_err(upipe, "could not connect to any resource");
        upipe_throw_error(upipe, UBASE_ERR_EXTERNAL);
        return UBASE_ERR_EXTERNAL;
    }

    upipe_http_src->hook =
        http_src_hook_init(&upipe_http_src->http_hook, flow_def);

    struct upipe_http_src_hook *hook = NULL;
    ret = upipe_http_src_throw_scheme_hook(upipe, flow_def, &hook);
    if (!ubase_check(ret) || !hook)
        hook = http_src_hook_init(&upipe_http_src->http_hook, flow_def);

    upipe_http_src->hook = hook;
    upipe_http_src->fd = fd;
    return UBASE_ERR_NONE;
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
    int ret;

    upipe_http_src_close(upipe);

    if (unlikely(url == NULL))
        return UBASE_ERR_NONE;

    upipe_notice_va(upipe, "opening %s", url);

    upipe_http_src_demand_uref_mgr(upipe);
    if (unlikely(upipe_http_src->uref_mgr == NULL)) {
        upipe_err(upipe, "no uref mgr");
        return UBASE_ERR_ALLOC;
    }

    struct uref *flow_def =
        uref_block_flow_alloc_def(upipe_http_src->uref_mgr, NULL);
    if (unlikely(flow_def == NULL)) {
        upipe_err(upipe, "fail to create flow def");
        return UBASE_ERR_NONE;
    }

    ret = uref_uri_set_from_str(flow_def, url);
    if (unlikely(!ubase_check(ret))) {
        uref_free(flow_def);
        return ret;
    }

    upipe_http_src_store_flow_def(upipe, flow_def);

    upipe_http_src->url = strdup(url);
    if (unlikely(upipe_http_src->url == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    /* now call real code */
    UBASE_RETURN(upipe_http_src_open_url(upipe));
    return upipe_http_src_send_request(upipe);
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

static int _upipe_http_src_set_proxy(struct upipe *upipe, const char *proxy)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);

    ubase_clean_str(&upipe_http_src->proxy);
    if (proxy == NULL)
        return UBASE_ERR_NONE;

    upipe_http_src->proxy = strdup(proxy);
    if (unlikely(upipe_http_src->proxy == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the read/write timeout.
 *
 * @param upipe description structure of the pipe
 * @param timeout timeout value in 27MHz clock ticks
 * @return an error code
 */
static int _upipe_http_src_set_timeout(struct upipe *upipe, uint64_t timeout)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    if (timeout != upipe_http_src->timeout) {
        upipe_http_src->timeout = timeout;
        upipe_http_src_set_upump_timeout(upipe, NULL);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This gets the user agent to use.
 *
 * @param upipe description structure of the pipe
 * @param user_agent_p filled with the current user agent
 * @return an error code
 */
static int _upipe_http_src_get_user_agent(struct upipe *upipe,
                                          const char **user_agent_p)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    if (user_agent_p)
        *user_agent_p = upipe_http_src->user_agent;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the user agent to use.
 *
 * @param upipe description structure of the pipe
 * @param user_agent user agent to use
 * @return an error code
 */
static int _upipe_http_src_set_user_agent(struct upipe *upipe,
                                          const char *user_agent)
{
    struct upipe_http_src *upipe_http_src = upipe_http_src_from_upipe(upipe);
    free(upipe_http_src->user_agent);
    upipe_http_src->user_agent = NULL;
    if (!user_agent)
        return UBASE_ERR_NONE;
    upipe_http_src->user_agent = strdup(user_agent);
    if (!upipe_http_src->user_agent)
        return UBASE_ERR_ALLOC;
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
            upipe_http_src_set_upump_read(upipe, NULL);
            upipe_http_src_set_upump_write(upipe, NULL);
            upipe_http_src_set_upump_timeout(upipe, NULL);
            upipe_http_src_set_upump_data_in(upipe, NULL);
            upipe_http_src_set_upump_data_out(upipe, NULL);
            return upipe_http_src_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_http_src_set_upump_read(upipe, NULL);
            upipe_http_src_set_upump_write(upipe, NULL);
            upipe_http_src_set_upump_timeout(upipe, NULL);
            upipe_http_src_set_upump_data_in(upipe, NULL);
            upipe_http_src_set_upump_data_out(upipe, NULL);
            upipe_http_src_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_GET_FLOW_DEF:
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
            return upipe_http_src_control_output(upipe, command, args);

        case UPIPE_GET_OUTPUT_SIZE:
        case UPIPE_SET_OUTPUT_SIZE:
            return upipe_http_src_control_output_size(upipe, command, args);

        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_http_src_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_http_src_set_uri(upipe, uri);
        }

        case UPIPE_SRC_GET_POSITION: {
            uint64_t *position_p = va_arg(args, uint64_t *);
            return _upipe_http_src_get_position(upipe, position_p);
        }
        case UPIPE_SRC_SET_POSITION: {
            uint64_t offset = va_arg(args, uint64_t);
            return _upipe_http_src_set_position(upipe, offset);
        }

        case UPIPE_SRC_SET_RANGE: {
            uint64_t offset = va_arg(args, uint64_t);
            uint64_t length = va_arg(args, uint64_t);
            return _upipe_http_src_set_range(upipe, offset, length);
        }

        case UPIPE_HTTP_SRC_SET_PROXY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            const char *proxy = va_arg(args, const char *);
            return _upipe_http_src_set_proxy(upipe, proxy);
        }

        case UPIPE_HTTP_SRC_SET_TIMEOUT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            uint64_t timeout = va_arg(args, uint64_t);
            return _upipe_http_src_set_timeout(upipe, timeout);
        }

        case UPIPE_HTTP_SRC_GET_USER_AGENT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            const char **user_agent_p = va_arg(args, const char **);
            return _upipe_http_src_get_user_agent(upipe, user_agent_p);
        }
        case UPIPE_HTTP_SRC_SET_USER_AGENT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
            const char *user_agent = va_arg(args, const char *);
            return _upipe_http_src_set_user_agent(upipe, user_agent);
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

struct upipe_http_src_mgr {
    /** upipe manager */
    struct upipe_mgr upipe_mgr;
    /** urefcount structure */
    struct urefcount urefcount;
    /** cookie list */
    struct uchain cookies;
    /** proxy url */
    char *proxy;
    /** user agent */
    char *user_agent;
};

UBASE_FROM_TO(upipe_http_src_mgr, upipe_mgr, upipe_mgr, upipe_mgr)
UBASE_FROM_TO(upipe_http_src_mgr, urefcount, urefcount, urefcount);

static int _upipe_http_src_mgr_set_cookie(struct upipe_mgr *upipe_mgr,
                                          const char *cookie_string)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        upipe_http_src_mgr_from_upipe_mgr(upipe_mgr);

    if (!cookie_string)
        return UBASE_ERR_INVALID;

    //FIXME: use uref ?
    struct upipe_http_src_cookie *cookie = malloc(sizeof (*cookie));
    if (unlikely(cookie == NULL))
        return UBASE_ERR_ALLOC;

    cookie->value = strdup(cookie_string);
    if (unlikely(cookie->value == NULL)) {
        free(cookie);
        return UBASE_ERR_ALLOC;
    }
    if (!ubase_check(ucookie_from_str(&cookie->ucookie, cookie->value))) {
        free(cookie->value);
        free(cookie);
        return UBASE_ERR_INVALID;
    }

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_http_src_mgr->cookies, uchain, uchain_tmp) {
        struct upipe_http_src_cookie *item =
            upipe_http_src_cookie_from_uchain(uchain);

        if (!ustring_cmp(item->ucookie.name, cookie->ucookie.name)) {
            ulist_delete(uchain);
            free(item->value);
            free(item);
        }
    }
    ulist_add(&upipe_http_src_mgr->cookies,
              upipe_http_src_cookie_to_uchain(cookie));
    return UBASE_ERR_NONE;
}

static bool upipe_http_src_domain_match(struct ustring domain,
                                        const char *string)
{
    return ustring_casematch_sfx(ustring_from_str(string), domain);
}

static bool upipe_http_src_path_match(struct ustring path,
                                      const char *string)
{
    return ustring_match(ustring_from_str(string), path);
}

static int _upipe_http_src_mgr_iterate_cookie(struct upipe_mgr *mgr,
                                              const char *domain,
                                              const char *path,
                                              struct uchain **uchain_p)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        upipe_http_src_mgr_from_upipe_mgr(mgr);

    struct uchain *first;
    if (!*uchain_p)
        first = &upipe_http_src_mgr->cookies;
    else
        first = *uchain_p;

    for (struct uchain *uchain = first->next;
         uchain != &upipe_http_src_mgr->cookies;
         uchain = uchain->next) {
        struct upipe_http_src_cookie *cookie =
            upipe_http_src_cookie_from_uchain(uchain);
        if (!upipe_http_src_domain_match(cookie->ucookie.domain, domain) ||
            !upipe_http_src_path_match(cookie->ucookie.path, path))
            continue;

        *uchain_p = uchain;
        return UBASE_ERR_NONE;
    }

    *uchain_p = NULL;
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_mgr_get_proxy(struct upipe_mgr *mgr,
                                         const char **proxy_p)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        upipe_http_src_mgr_from_upipe_mgr(mgr);
    if (proxy_p)
        *proxy_p = upipe_http_src_mgr->proxy;
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_mgr_set_proxy(struct upipe_mgr *mgr,
                                         const char *proxy)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        upipe_http_src_mgr_from_upipe_mgr(mgr);
    ubase_clean_str(&upipe_http_src_mgr->proxy);
    if (proxy == NULL)
        return UBASE_ERR_NONE;
    upipe_http_src_mgr->proxy = strdup(proxy);
    if (unlikely(upipe_http_src_mgr->proxy == NULL))
        return UBASE_ERR_ALLOC;
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_mgr_get_user_agent(struct upipe_mgr *mgr,
                                              const char **user_agent_p)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        upipe_http_src_mgr_from_upipe_mgr(mgr);
    if (user_agent_p)
        *user_agent_p = upipe_http_src_mgr->user_agent;
    return UBASE_ERR_NONE;
}

static int _upipe_http_src_mgr_set_user_agent(struct upipe_mgr *mgr,
                                              const char *user_agent)
{
    struct upipe_http_src_mgr  *upipe_http_src_mgr =
        upipe_http_src_mgr_from_upipe_mgr(mgr);

    free(upipe_http_src_mgr->user_agent);
    upipe_http_src_mgr->user_agent = NULL;
    if (!user_agent)
        return UBASE_ERR_NONE;
    upipe_http_src_mgr->user_agent = strdup(user_agent);
    if (!upipe_http_src_mgr->user_agent)
        return UBASE_ERR_ALLOC;
    return UBASE_ERR_NONE;
}

static int upipe_http_src_mgr_control(struct upipe_mgr *upipe_mgr,
                                      int command, va_list args)
{
    switch (command) {
    case UPIPE_HTTP_SRC_MGR_SET_COOKIE: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
        const char *cookie_string = va_arg(args, const char *);
        return _upipe_http_src_mgr_set_cookie(upipe_mgr, cookie_string);
    }
    case UPIPE_HTTP_SRC_MGR_ITERATE_COOKIE: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
        const char *domain = va_arg(args, const char *);
        const char *path = va_arg(args, const char *);
        struct uchain **uchain_p = va_arg(args, struct uchain **);
        return _upipe_http_src_mgr_iterate_cookie(upipe_mgr, domain, path,
                                                  uchain_p);
    }

    case UPIPE_HTTP_SRC_MGR_GET_PROXY: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
        const char **proxy_p = va_arg(args, const char **);
        return _upipe_http_src_mgr_get_proxy(upipe_mgr, proxy_p);
    }
    case UPIPE_HTTP_SRC_MGR_SET_PROXY: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
        const char *proxy = va_arg(args, const char *);
        return _upipe_http_src_mgr_set_proxy(upipe_mgr, proxy);
    }

    case UPIPE_HTTP_SRC_MGR_GET_USER_AGENT: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
        const char **user_agent_p = va_arg(args, const char **);
        return _upipe_http_src_mgr_get_user_agent(upipe_mgr, user_agent_p);
    }
    case UPIPE_HTTP_SRC_MGR_SET_USER_AGENT: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
        const char *user_agent = va_arg(args, const char *);
        return _upipe_http_src_mgr_set_user_agent(upipe_mgr, user_agent);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

static void upipe_http_src_mgr_free(struct urefcount *urefcount)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        upipe_http_src_mgr_from_urefcount(urefcount);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_http_src_mgr->cookies, uchain, uchain_tmp) {
        struct upipe_http_src_cookie *cookie =
            upipe_http_src_cookie_from_uchain(uchain);
        ulist_delete(uchain);
        free(cookie->value);
        free(cookie);
    }
    free(upipe_http_src_mgr->user_agent);
    free(upipe_http_src_mgr->proxy);
    urefcount_clean(urefcount);
    free(upipe_http_src_mgr);
}

/** @This returns the management structure for all http source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_http_src_mgr_alloc(void)
{
    struct upipe_http_src_mgr *upipe_http_src_mgr =
        malloc(sizeof (*upipe_http_src_mgr));
    if (unlikely(upipe_http_src_mgr == NULL))
        return NULL;
    struct upipe_mgr *upipe_mgr =
        upipe_http_src_mgr_to_upipe_mgr(upipe_http_src_mgr);

    struct urefcount *urefcount =
        upipe_http_src_mgr_to_urefcount(upipe_http_src_mgr);
    urefcount_init(urefcount, upipe_http_src_mgr_free);

    memset(upipe_mgr, 0, sizeof (struct upipe_mgr));
    *upipe_mgr = (struct upipe_mgr) {
        .signature = UPIPE_HTTP_SRC_SIGNATURE,
        .upipe_event_str = uprobe_http_src_event_str,
        .upipe_alloc = upipe_http_src_alloc,
        .upipe_control = upipe_http_src_control,
        .upipe_mgr_control = upipe_http_src_mgr_control,
    };
    upipe_mgr->refcount = urefcount;
    ulist_init(&upipe_http_src_mgr->cookies);
    upipe_http_src_mgr->proxy = NULL;
    upipe_http_src_mgr->user_agent = strdup(USER_AGENT);
    if (unlikely(!upipe_http_src_mgr->user_agent)) {
        upipe_mgr_release(upipe_http_src_mgr_to_upipe_mgr(upipe_http_src_mgr));
        return NULL;
    }

    return upipe_http_src_mgr_to_upipe_mgr(upipe_http_src_mgr);
}

/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
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
 * @short HTTPS hooks for SSL data read/write.
 */

#include <errno.h>
#include <bearssl.h>

#include <upipe/ubase.h>
#include <upipe/uref_uri.h>
#include <upipe/urefcount_helper.h>

#include "https_source_hook.h"

/** This describes a x509 no anchor context to allow not trusted certificate. */
struct x509_noanchor_context {
    const br_x509_class *vtable;
    const br_x509_class **inner;
};

/** @This describes a SSL context for HTTPS. */
struct https_src_hook {
    /** public hook structure */
    struct upipe_http_src_hook hook;
    /** refcount */
    struct urefcount urefcount;
    /** client structure */
    br_ssl_client_context client;
    /** x509 context */
    br_x509_minimal_context x509;
    /** io buffer */
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    /** no anchor context */
    struct x509_noanchor_context x509_noanchor;
};

/** @hidden */
UREFCOUNT_HELPER(https_src_hook, urefcount, https_src_hook_free);
/** @hidden */
UBASE_FROM_TO(https_src_hook, upipe_http_src_hook, hook, hook);

/*
 * allow not trusted certificate
 */

/** @hidden */
static void xwc_start_chain(const br_x509_class **ctx, const char *server_name)
{
    struct x509_noanchor_context *xwc;

    xwc = (struct x509_noanchor_context *)ctx;
    (*xwc->inner)->start_chain(xwc->inner, server_name);
}

/** @hidden */
static void xwc_start_cert(const br_x509_class **ctx, uint32_t length)
{
    struct x509_noanchor_context *xwc;

    xwc = (struct x509_noanchor_context *)ctx;
    (*xwc->inner)->start_cert(xwc->inner, length);
}

/** @hidden */
static void xwc_append(const br_x509_class **ctx,
                       const unsigned char *buf, size_t len)
{
    struct x509_noanchor_context *xwc;

    xwc = (struct x509_noanchor_context *)ctx;
    (*xwc->inner)->append(xwc->inner, buf, len);
}

/** @hidden */
static void xwc_end_cert(const br_x509_class **ctx)
{
    struct x509_noanchor_context *xwc;

    xwc = (struct x509_noanchor_context *)ctx;
    (*xwc->inner)->end_cert(xwc->inner);
}

/** @hidden */
static unsigned xwc_end_chain(const br_x509_class **ctx)
{
    struct x509_noanchor_context *xwc;
    unsigned r;

    xwc = (struct x509_noanchor_context *)ctx;
    r = (*xwc->inner)->end_chain(xwc->inner);
    if (r == BR_ERR_X509_NOT_TRUSTED) {
        r = 0;
    }
    return r;
}

/** @hidden */
static const br_x509_pkey *xwc_get_pkey(const br_x509_class *const *ctx,
                                        unsigned *usages)
{
    struct x509_noanchor_context *xwc;

    xwc = (struct x509_noanchor_context *)ctx;
    return (*xwc->inner)->get_pkey(xwc->inner, usages);
}

/** @hidden */
static void x509_noanchor_init(struct x509_noanchor_context *xwc,
                               const br_x509_class **inner)
{
    static const br_x509_class x509_noanchor_vtable = {
        sizeof(struct x509_noanchor_context),
        xwc_start_chain,
        xwc_start_cert,
        xwc_append,
        xwc_end_cert,
        xwc_end_chain,
        xwc_get_pkey
    };

    xwc->vtable = &x509_noanchor_vtable;
    xwc->inner = inner;
}

/** @internal @This converts BearSSL state to upipe state.
 *
 * @param state BearSSL state
 * @return a upipe http source hook state
 */
static int https_src_hook_state_to_code(unsigned state)
{
    int flags = 0;
    if (state & BR_SSL_SENDREC)
        flags |= UPIPE_HTTP_SRC_HOOK_TRANSPORT_WRITE;
    if (state & BR_SSL_RECVREC)
        flags |= UPIPE_HTTP_SRC_HOOK_TRANSPORT_READ;
    if (state & BR_SSL_SENDAPP)
        flags |= UPIPE_HTTP_SRC_HOOK_DATA_WRITE;
    if (state & BR_SSL_RECVAPP)
        flags |= UPIPE_HTTP_SRC_HOOK_DATA_READ;
    return flags;
}

/** @internal @This reads from the socket to the SSL engine.
 *
 * @param hook SSL hook structure
 * @param fd socket file descriptor
 * @return 0 or negative value on error, 1 if more data is needed, 2 otherwise
 */
static int
https_src_hook_transport_read(struct upipe_http_src_hook *hook, int fd)
{
    struct https_src_hook *https = https_src_hook_from_hook(hook);
    br_ssl_engine_context *eng = &https->client.eng;

    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_RECVREC) {
        size_t size;
        unsigned char *buf = br_ssl_engine_recvrec_buf(eng, &size);
        ssize_t rlen = read(fd, buf, size);
        if (rlen <= 0)
            return rlen;

        br_ssl_engine_recvrec_ack(eng, rlen);
        state = br_ssl_engine_current_state(eng);
    }

    return https_src_hook_state_to_code(state);
}

/** @internal @This writes from the SSL engine to the socket.
 *
 * @param hook SSL hook structure
 * @param fd socket file descriptor
 * @return 0 or negative value on error, 1 if more data is needed, 2 otherwise
 */
static int
https_src_hook_transport_write(struct upipe_http_src_hook *hook, int fd)
{
    struct https_src_hook *https = https_src_hook_from_hook(hook);
    br_ssl_engine_context *eng = &https->client.eng;

    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_SENDREC) {
        size_t size;
        unsigned char *buf = br_ssl_engine_sendrec_buf(eng, &size);
        ssize_t wlen = write(fd, buf, size);
        if (wlen <= 0)
            return wlen;
        br_ssl_engine_sendrec_ack(eng, wlen);
        state = br_ssl_engine_current_state(eng);
    }

    return https_src_hook_state_to_code(state);
}

/** @internal @This reads data from the SSL engine to a buffer.
 *
 * @param hook SSL hook structure
 * @param buffer filled with data
 * @param count buffer size
 * @return a negative value on error, 0 if the connection is closed, the number
 * of bytes written to the buffer
 */
static ssize_t https_src_hook_data_read(struct upipe_http_src_hook *hook,
                                        uint8_t *buffer, size_t count)
{
    struct https_src_hook *https = https_src_hook_from_hook(hook);
    br_ssl_engine_context *eng = &https->client.eng;
    ssize_t rsize = -1;

    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_RECVAPP) {
        size_t size;
        unsigned char *buf = br_ssl_engine_recvapp_buf(eng, &size);
        rsize = size > count ? count : size;
        memcpy(buffer, buf, rsize);
        br_ssl_engine_recvapp_ack(eng, rsize);
    }
    else if (state & BR_SSL_CLOSED)
        rsize = 0;
    else
        errno = EAGAIN;

    return rsize;
}

/** @internal @This writes data from a buffer to the SSL engine.
 *
 * @param hook SSL hook structure
 * @param buffer data to write
 * @param count buffer number of bytes in the buffer
 * @return a negative value on error or the number of bytes read from the buffer
 */
static ssize_t https_src_hook_data_write(struct upipe_http_src_hook *hook,
                                         const uint8_t *buffer, size_t count)
{
    struct https_src_hook *https = https_src_hook_from_hook(hook);
    br_ssl_engine_context *eng = &https->client.eng;
    ssize_t wsize = -1;

    unsigned state = br_ssl_engine_current_state(eng);
    if (state & BR_SSL_SENDAPP) {
        size_t size;
        unsigned char *buf = br_ssl_engine_sendapp_buf(eng, &size);
        wsize = size > count ? count : size;
        memcpy(buf, buffer, wsize);
        br_ssl_engine_sendapp_ack(eng, wsize);
        if (wsize == count)
            br_ssl_engine_flush(eng, 1);
        state = br_ssl_engine_current_state(eng);
    }
    else {
        errno = EAGAIN;
    }

    return wsize;
}

/** @This is called when there is no more reference on the hook.
 *
 * @param https https source hook
 */
static void https_src_hook_free(struct https_src_hook *https)
{
    https_src_hook_clean_urefcount(https);
    free(https);
}

/** @This initializes the ssl context.
 *
 * @param https private SSL HTTPS context to initialize
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *https_src_hook_alloc(struct uref *flow_def)
{
    struct https_src_hook *https = malloc(sizeof (*https));
    if (unlikely(!https))
        return NULL;

    const char *host = NULL;
    int err = uref_uri_get_host(flow_def, &host);
    if (!ubase_check(err) || !host) {
        free(https);
        return NULL;
    }

    br_ssl_client_init_full(&https->client, &https->x509, NULL, 0);
    x509_noanchor_init(&https->x509_noanchor, &https->x509.vtable);
    br_ssl_engine_set_x509(&https->client.eng, &https->x509_noanchor.vtable);
    br_ssl_engine_set_buffer(&https->client.eng, https->iobuf,
                             sizeof (https->iobuf), 1);
    br_ssl_client_reset(&https->client, host, 0);
    https_src_hook_init_urefcount(https);
    https->hook.urefcount = &https->urefcount;
    https->hook.transport.read = https_src_hook_transport_read;
    https->hook.transport.write = https_src_hook_transport_write;
    https->hook.data.read = https_src_hook_data_read;
    https->hook.data.write = https_src_hook_data_write;
    return &https->hook;
}

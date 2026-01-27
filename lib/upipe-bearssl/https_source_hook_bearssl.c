/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
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
 * @short BearSSL HTTPS hooks for SSL data read/write.
 */

#include <errno.h>
#include <bearssl.h>

#include "upipe/ubase.h"
#include "upipe/uref_uri.h"
#include "upipe/urefcount_helper.h"

#include "https_source_hook_bearssl.h"

static const char *error_string(int error_code)
{
    switch (error_code) {
        case BR_ERR_OK:
            return "OK";
        case BR_ERR_BAD_PARAM:
            return "BAD_PARAM";
        case BR_ERR_BAD_STATE:
            return "BAD_STATE";
        case BR_ERR_UNSUPPORTED_VERSION:
            return "UNSUPPORTED_VERSION";
        case BR_ERR_BAD_VERSION:
            return "BAD_VERSION";
        case BR_ERR_BAD_LENGTH:
            return "BAD_LENGTH";
        case BR_ERR_TOO_LARGE:
            return "TOO_LARGE";
        case BR_ERR_BAD_MAC:
            return "BAD_MAC";
        case BR_ERR_NO_RANDOM:
            return "NO_RANDOM";
        case BR_ERR_UNKNOWN_TYPE:
            return "UNKNOWN_TYPE";
        case BR_ERR_UNEXPECTED:
            return "UNEXPECTED";
        case BR_ERR_BAD_CCS:
            return "BAD_CCS";
        case BR_ERR_BAD_ALERT:
            return "BAD_ALERT";
        case BR_ERR_BAD_HANDSHAKE:
            return "BAD_HANDSHAKE";
        case BR_ERR_OVERSIZED_ID:
            return "OVERSIZED_ID";
        case BR_ERR_BAD_CIPHER_SUITE:
            return "BAD_CIPHER_SUITE";
        case BR_ERR_BAD_COMPRESSION:
            return "BAD_COMPRESSION";
        case BR_ERR_BAD_FRAGLEN:
            return "BAD_FRAGLEN";
        case BR_ERR_BAD_SECRENEG:
            return "BAD_SECRENEG";
        case BR_ERR_EXTRA_EXTENSION:
            return "EXTRA_EXTENSION";
        case BR_ERR_BAD_SNI:
            return "BAD_SNI";
        case BR_ERR_BAD_HELLO_DONE:
            return "BAD_HELLO_DONE";
        case BR_ERR_LIMIT_EXCEEDED:
            return "LIMIT_EXCEEDED";
        case BR_ERR_BAD_FINISHED:
            return "BAD_FINISHED";
        case BR_ERR_RESUME_MISMATCH:
            return "RESUME_MISMATCH";
        case BR_ERR_INVALID_ALGORITHM:
            return "INVALID_ALGORITHM";
        case BR_ERR_BAD_SIGNATURE:
            return "BAD_SIGNATURE";
        case BR_ERR_WRONG_KEY_USAGE:
            return "WRONG_KEY_USAGE";
        case BR_ERR_NO_CLIENT_AUTH:
            return "NO_CLIENT_AUTH";
        case BR_ERR_IO:
            return "IO";
        case BR_ERR_RECV_FATAL_ALERT:
            return "RECV_FATAL_ALERT";
        case BR_ERR_SEND_FATAL_ALERT:
            return "SEND_FATAL_ALERT";
        case BR_ERR_X509_OK:
            return "X509_OK";
        case BR_ERR_X509_INVALID_VALUE:
            return "X509_INVALID_VALUE";
        case BR_ERR_X509_TRUNCATED:
            return "X509_TRUNCATED";
        case BR_ERR_X509_EMPTY_CHAIN:
            return "X509_EMPTY_CHAIN";
        case BR_ERR_X509_INNER_TRUNC:
            return "X509_INNER_TRUNC";
        case BR_ERR_X509_BAD_TAG_CLASS:
            return "X509_BAD_TAG_CLASS";
        case BR_ERR_X509_BAD_TAG_VALUE:
            return "X509_BAD_TAG_VALUE";
        case BR_ERR_X509_INDEFINITE_LENGTH:
            return "X509_INDEFINITE_LENGTH";
        case BR_ERR_X509_EXTRA_ELEMENT:
            return "X509_EXTRA_ELEMENT";
        case BR_ERR_X509_UNEXPECTED:
            return "X509_UNEXPECTED";
        case BR_ERR_X509_NOT_CONSTRUCTED:
            return "X509_NOT_CONSTRUCTED";
        case BR_ERR_X509_NOT_PRIMITIVE:
            return "X509_NOT_PRIMITIVE";
        case BR_ERR_X509_PARTIAL_BYTE:
            return "X509_PARTIAL_BYTE";
        case BR_ERR_X509_BAD_BOOLEAN:
            return "X509_BAD_BOOLEAN";
        case BR_ERR_X509_OVERFLOW:
            return "X509_OVERFLOW";
        case BR_ERR_X509_BAD_DN:
            return "X509_BAD_DN";
        case BR_ERR_X509_BAD_TIME:
            return "X509_BAD_TIME";
        case BR_ERR_X509_UNSUPPORTED:
            return "X509_UNSUPPORTED";
        case BR_ERR_X509_LIMIT_EXCEEDED:
            return "X509_LIMIT_EXCEEDED";
        case BR_ERR_X509_WRONG_KEY_TYPE:
            return "X509_WRONG_KEY_TYPE";
        case BR_ERR_X509_BAD_SIGNATURE:
            return "X509_BAD_SIGNATURE";
        case BR_ERR_X509_TIME_UNKNOWN:
            return "X509_TIME_UNKNOWN";
        case BR_ERR_X509_EXPIRED:
            return "X509_EXPIRED";
        case BR_ERR_X509_DN_MISMATCH:
            return "X509_DN_MISMATCH";
        case BR_ERR_X509_BAD_SERVER_NAME:
            return "X509_BAD_SERVER_NAME";
        case BR_ERR_X509_CRITICAL_EXTENSION:
            return "X509_CRITICAL_EXTENSION";
        case BR_ERR_X509_NOT_CA:
            return "X509_NOT_CA";
        case BR_ERR_X509_FORBIDDEN_KEY_USAGE:
            return "X509_FORBIDDEN_KEY_USAGE";
        case BR_ERR_X509_WEAK_PUBLIC_KEY:
            return "X509_WEAK_PUBLIC_KEY";
        case BR_ERR_X509_NOT_TRUSTED:
            return "X509_NOT_TRUSTED";
    }
    return "Unknown error";
}

/** This describes a x509 no anchor context to allow not trusted certificate. */
struct x509_noanchor_context {
    const br_x509_class *vtable;
    const br_x509_class **inner;
};

/** @This describes a SSL context for HTTPS. */
struct https_src_hook_bearssl {
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
UREFCOUNT_HELPER(https_src_hook_bearssl, urefcount,
                 https_src_hook_bearssl_free);
/** @hidden */
UBASE_FROM_TO(https_src_hook_bearssl, upipe_http_src_hook, hook, hook);

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
    if (state & BR_SSL_RECVAPP || state & BR_SSL_CLOSED)
        flags |= UPIPE_HTTP_SRC_HOOK_DATA_READ;
    return flags;
}

/** @internal @This reads from the socket to the SSL engine.
 *
 * @param upipe description structure of the pipe
 * @param hook SSL hook structure
 * @param fd socket file descriptor
 * @return 0 or negative value on error, 1 if more data is needed, 2 otherwise
 */
static int
https_src_hook_transport_read(struct upipe *upipe,
                              struct upipe_http_src_hook *hook, int fd)
{
    struct https_src_hook_bearssl *https =
        https_src_hook_bearssl_from_hook(hook);
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
 * @param upipe description structure of the pipe
 * @param hook SSL hook structure
 * @param fd socket file descriptor
 * @return 0 or negative value on error, 1 if more data is needed, 2 otherwise
 */
static int
https_src_hook_transport_write(struct upipe *upipe,
                               struct upipe_http_src_hook *hook, int fd)
{
    struct https_src_hook_bearssl *https =
        https_src_hook_bearssl_from_hook(hook);
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
 * @param upipe description structure of the pipe
 * @param hook SSL hook structure
 * @param buffer filled with data
 * @param count buffer size
 * @return a negative value on error, 0 if the connection is closed, the number
 * of bytes written to the buffer
 */
static ssize_t https_src_hook_data_read(struct upipe *upipe,
                                        struct upipe_http_src_hook *hook,
                                        uint8_t *buffer, size_t count)
{
    struct https_src_hook_bearssl *https =
        https_src_hook_bearssl_from_hook(hook);
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
    else if (state & BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(eng);
        if (err) {
            upipe_err_va(upipe, "connection failed (%s)", error_string(err));
            errno = EIO;
        }
        rsize = err ? -1 : 0;
    }
    else
        errno = EAGAIN;

    return rsize;
}

/** @internal @This writes data from a buffer to the SSL engine.
 *
 * @param upipe description structure of the pipe
 * @param hook SSL hook structure
 * @param buffer data to write
 * @param count buffer number of bytes in the buffer
 * @return a negative value on error or the number of bytes read from the buffer
 */
static ssize_t https_src_hook_data_write(struct upipe *upipe,
                                         struct upipe_http_src_hook *hook,
                                         const uint8_t *buffer, size_t count)
{
    struct https_src_hook_bearssl *https =
        https_src_hook_bearssl_from_hook(hook);
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
static void https_src_hook_bearssl_free(struct https_src_hook_bearssl *https)
{
    https_src_hook_bearssl_clean_urefcount(https);
    free(https);
}

/** @This initializes the ssl context.
 *
 * @param https private SSL HTTPS context to initialize
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *https_src_hook_bearssl_alloc(struct uref *flow_def)
{
    struct https_src_hook_bearssl *https = malloc(sizeof (*https));
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
    https_src_hook_bearssl_init_urefcount(https);
    https->hook.urefcount = &https->urefcount;
    https->hook.transport.read = https_src_hook_transport_read;
    https->hook.transport.write = https_src_hook_transport_write;
    https->hook.data.read = https_src_hook_data_read;
    https->hook.data.write = https_src_hook_data_write;
    return &https->hook;
}

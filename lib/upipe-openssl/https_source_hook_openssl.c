/*
 * Copyright (C) 2024 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the righs to use, copy, modify, merge, publish,
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
 * @short OpenSSL HTTPS hooks for SSL data read/write.
 */

#include <errno.h>
#include <openssl/ssl.h>

#include "upipe/ubase.h"
#include "upipe/uref_uri.h"
#include "upipe/urefcount_helper.h"

#include "https_source_hook_openssl.h"

#define BUFFER_SIZE 4096

/** @This describes a SSL context for HTTPS. */
struct https_src_hook_openssl {
    /** public hook structure */
    struct upipe_http_src_hook hook;
    /** refcount */
    struct urefcount urefcount;
    /* OpenSSL context */
    SSL_CTX *ssl_ctx;
    /* SSL engine */
    SSL *ssl;
    /* SSL read IO */
    BIO *rbio;
    /* SSL write IO */
    BIO *wbio;
    /* output transport buffer */
    uint8_t out[BUFFER_SIZE];
    /* current output transport buffer size */
    size_t out_size;
};

/** @hidden */
UREFCOUNT_HELPER(https_src_hook_openssl, urefcount,
                 https_src_hook_openssl_free);
/** @hidden */
UBASE_FROM_TO(https_src_hook_openssl, upipe_http_src_hook, hook, hook);

static int https_src_hook_flags(struct https_src_hook_openssl *https)
{
    /* we always want to read data if any */
    int flags = UPIPE_HTTP_SRC_HOOK_TRANSPORT_READ;

    if (!SSL_is_init_finished(https->ssl)) {
        int ret = SSL_do_handshake(https->ssl);
        if (ret == 1) {
            /* connected, so we can write data */
            flags |= UPIPE_HTTP_SRC_HOOK_DATA_WRITE;
        } else {
            ret = SSL_get_error(https->ssl, ret);
            if (ret == SSL_ERROR_WANT_READ ||
                ret == SSL_ERROR_WANT_WRITE) {
                size_t available = BUFFER_SIZE - https->out_size;
                while (available) {
                    ret = BIO_read(https->wbio, https->out + https->out_size, available);
                    if (ret <= 0) {
                        break;
                    }
                    https->out_size += ret;
                    available -= ret;
                }
            }
        }
    }

    if (https->out_size)
        /* there is still buffered data to send */
        flags |= UPIPE_HTTP_SRC_HOOK_TRANSPORT_WRITE;
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
    struct https_src_hook_openssl *https =
        https_src_hook_openssl_from_hook(hook);
    uint8_t buffer[BUFFER_SIZE];
    ssize_t rsize = read(fd, buffer, sizeof (buffer));
    if (rsize <= 0)
        return rsize;

    int ret = BIO_write(https->rbio, buffer, rsize);
    if (ret <= 0)
        return -1;

    int flags = 0;
    if (SSL_is_init_finished(https->ssl))
        /* we are already connected so we may now have some data */
        flags |= UPIPE_HTTP_SRC_HOOK_DATA_READ;
    return flags | https_src_hook_flags(https);
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
    struct https_src_hook_openssl *https =
        https_src_hook_openssl_from_hook(hook);

    size_t available = BUFFER_SIZE - https->out_size;
    if (available) {
        int ret =
            BIO_read(https->wbio, https->out + https->out_size, available);
        if (ret <= 0) {
            if (!BIO_should_retry(https->wbio))
                return -1;
        } else
            https->out_size += ret;
    }

    if (https->out_size) {
        ssize_t ret = write(fd, https->out, https->out_size);
        if (ret <= 0)
            return ret;
        https->out_size -= ret;
        if (https->out_size)
            memmove(https->out, https->out + ret, https->out_size);
    }

    return https_src_hook_flags(https);
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
    struct https_src_hook_openssl *https =
        https_src_hook_openssl_from_hook(hook);
    size_t rsize = 0;
    int ret = -1;

    while (rsize < count) {
        ret = SSL_read(https->ssl, buffer + rsize, count - rsize);
        if (ret <= 0)
            break;
        rsize += ret;
    }
    if (rsize)
        return rsize;

    ret = SSL_get_error(https->ssl, ret);
    if (ret == SSL_ERROR_WANT_READ || ret == SSL_ERROR_WANT_WRITE)
        errno = EAGAIN;
    else if (ret == SSL_ERROR_ZERO_RETURN)
        return 0;
    else
        errno = EIO;
    return -1;
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
    struct https_src_hook_openssl *https =
        https_src_hook_openssl_from_hook(hook);
    size_t wsize = 0;
    int ret = -1;

    while (wsize < count) {
        ret = SSL_write(https->ssl, buffer + wsize, count - wsize);
        if (ret <= 0)
            break;
        wsize += ret;
    }
    if (wsize)
        return wsize;

    ret = SSL_get_error(https->ssl, ret);
    if (ret == SSL_ERROR_WANT_READ || ret == SSL_ERROR_WANT_WRITE)
        errno = EAGAIN;
    else
        errno = EIO;
    return -1;
}

/** @This is called when there is no more reference on the hook.
 *
 * @param https https source hook
 */
static void https_src_hook_openssl_free(struct https_src_hook_openssl *https)
{
    https_src_hook_openssl_clean_urefcount(https);
    SSL_free(https->ssl);
    SSL_CTX_free(https->ssl_ctx);
    free(https);
}

/** @This initializes the ssl context.
 *
 * @param https private SSL HTTPS context to initialize
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *https_src_hook_openssl_alloc(struct uref *flow_def)
{
    struct https_src_hook_openssl *https = malloc(sizeof (*https));
    if (unlikely(!https))
        return NULL;

    const char *host = NULL;
    int err = uref_uri_get_host(flow_def, &host);
    if (!ubase_check(err) || !host) {
        free(https);
        return NULL;
    }

    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (unlikely(ssl_ctx == NULL)) {
        free(https);
        return NULL;
    }

    /* Recommended to avoid SSLv2 & SSLv3 */
    SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL);

    SSL *ssl = SSL_new(ssl_ctx);
    if (unlikely(ssl == NULL)) {
        SSL_CTX_free(ssl_ctx);
        free(https);
        return NULL;
    }
    SSL_set_connect_state(ssl);
    SSL_set_tlsext_host_name(ssl, host); // TLS SNI

    BIO *rbio = BIO_new(BIO_s_mem());
    BIO *wbio = BIO_new(BIO_s_mem());
    if (!rbio || !wbio) {
        BIO_free(rbio);
        BIO_free(wbio);
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }
    SSL_set_bio(ssl, rbio, wbio);

    https->ssl_ctx = ssl_ctx;
    https->ssl = ssl;
    https->rbio = rbio;
    https->wbio = wbio;
    https->out_size = 0;

    https_src_hook_openssl_init_urefcount(https);
    https->hook.urefcount = &https->urefcount;
    https->hook.transport.read = https_src_hook_transport_read;
    https->hook.transport.write = https_src_hook_transport_write;
    https->hook.data.read = https_src_hook_data_read;
    https->hook.data.write = https_src_hook_data_write;
    return &https->hook;
}

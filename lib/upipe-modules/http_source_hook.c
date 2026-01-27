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
 * @short HTTP hooks for plain data read/write.
 */

#include <string.h>
#include <errno.h>

#include "upipe/ubase.h"

#include "http_source_hook.h"

/** @hidden */
UBASE_FROM_TO(http_src_hook, upipe_http_src_hook, hook, hook);

static int http_src_hook_state(struct http_src_hook *http)
{
    int flags = 0;
    if (http->out.len < UPIPE_HTTP_SRC_HOOK_BUFFER)
        flags |= UPIPE_HTTP_SRC_HOOK_TRANSPORT_READ;
    if (http->in.len)
        flags |= UPIPE_HTTP_SRC_HOOK_TRANSPORT_WRITE;
    if (http->out.len)
        flags |= UPIPE_HTTP_SRC_HOOK_DATA_READ;
    if (http->in.len < UPIPE_HTTP_SRC_HOOK_BUFFER)
        flags |= UPIPE_HTTP_SRC_HOOK_DATA_WRITE;
    return flags;
}

/** @internal @This reads from the socket to the plain engine.
 *
 * @param upipe description structure of the pipe
 * @param hook plain hook structure
 * @param fd socket file descriptor
 * @return 0 or negative value on error, 1 if more data is needed, 2 otherwise
 */
static int
http_src_hook_transport_read(struct upipe *upipe,
                             struct upipe_http_src_hook *hook, int fd)
{
    struct http_src_hook *http = http_src_hook_from_hook(hook);
    size_t size = UPIPE_HTTP_SRC_HOOK_BUFFER - http->out.len;
    if (size) {
        ssize_t rsize = read(fd, http->out.buf + http->out.len, size);
        if (rsize < 0) {
            upipe_err_va(upipe, "read error (%s)", strerror(errno));
            return -1;
        } else if (rsize == 0) {
            http->closed = true;
            return 0;
        }
        http->out.len += rsize;
    }
    return http_src_hook_state(http);
}

/** @internal @This writes from the plain engine to the socket.
 *
 * @param upipe description structure of the pipe
 * @param hook plain hook structure
 * @param fd socket file descriptor
 * @return 0 or negative value on error, 1 if more data is needed, 2 otherwise
 */
static int
http_src_hook_transport_write(struct upipe *upipe,
                              struct upipe_http_src_hook *hook, int fd)
{
    struct http_src_hook *http = http_src_hook_from_hook(hook);
    size_t size = http->in.len;
    ssize_t wsize = -1;
    if (size) {
        wsize = write(fd, http->in.buf, size);
        if (wsize < 0) {
            upipe_err_va(upipe, "write error (%s)", strerror(errno));
            return -1;
        } else if (wsize == 0) {
            http->closed = true;
            return 0;
        }
        memmove(http->in.buf, http->in.buf + wsize, size - wsize);
        http->in.len -= wsize;
    }

    return http_src_hook_state(http);
}

/** @internal @This reads data from the plain engine to a buffer.
 *
 * @param upipe description structure of the pipe
 * @param hook plain hook structure
 * @param buffer filled with data
 * @param count buffer size
 * @return a negative value on error, 0 if the connection is closed, the number
 * of bytes written to the buffer
 */
static ssize_t http_src_hook_data_read(struct upipe *upipe,
                                       struct upipe_http_src_hook *hook,
                                       uint8_t *buffer, size_t count)
{
    struct http_src_hook *http = http_src_hook_from_hook(hook);
    size_t size = count > http->out.len ? http->out.len : count;
    if (size) {
        memcpy(buffer, http->out.buf, size);
        http->out.len -= size;
        memmove(http->out.buf, http->out.buf + size, http->out.len);
        return size;
    }
    if (http->closed)
        return 0;
    errno = EAGAIN;
    return -1;
}

/** @internal @This writes data from a buffer to the plain engine.
 *
 * @param upipe description structure of the pipe
 * @param hook plain hook structure
 * @param buffer data to write
 * @param count buffer number of bytes in the buffer
 * @return a negative value on error or the number of bytes read from the buffer
 */
static ssize_t http_src_hook_data_write(struct upipe *upipe,
                                        struct upipe_http_src_hook *hook,
                                        const uint8_t *buffer, size_t count)
{
    struct http_src_hook *http = http_src_hook_from_hook(hook);
    size_t size = UPIPE_HTTP_SRC_HOOK_BUFFER - http->in.len;
    if (size) {
        if (size > count)
            size = count;
        memcpy(http->in.buf + http->in.len, buffer, size);
        http->in.len += size;
        return size;
    }
    errno = EAGAIN;
    return -1;
}

/** @This initializes the plain context.
 *
 * @param http private plain HTTP context to initialize
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *http_src_hook_init(struct http_src_hook *http,
                                               struct uref *flow_def)
{
    http->hook.urefcount = NULL;
    http->hook.transport.read = http_src_hook_transport_read;
    http->hook.transport.write = http_src_hook_transport_write;
    http->hook.data.read = http_src_hook_data_read;
    http->hook.data.write = http_src_hook_data_write;
    http->in.len = 0;
    http->out.len = 0;
    http->closed = false;
    return &http->hook;
};

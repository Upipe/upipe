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
 * @short HTTP hooks for plain data read/write.
 */

#ifndef _UPIPE_MODULES_HTTP_SOURCE_HOOK_H_
#define _UPIPE_MODULES_HTTP_SOURCE_HOOK_H_

#include <upipe-modules/upipe_http_source.h>

#define UPIPE_HTTP_SRC_HOOK_BUFFER  4096

/** @This describes an internal buffer. */
struct http_src_hook_buffer {
    /** buffer */
    char buf[UPIPE_HTTP_SRC_HOOK_BUFFER];
    /** number of bytes in the buffer */
    size_t len;
};

/** @This describes a plain HTTP context. */
struct http_src_hook {
    /** public hook structure */
    struct upipe_http_src_hook hook;
    /** input buffer */
    struct http_src_hook_buffer in;
    /** output buffer */
    struct http_src_hook_buffer out;
    /** connection state */
    bool closed;
};

/** @This initializes the plain context.
 *
 * @param http private plain HTTP context to initialize
 * @param flow_def connection attributes
 * @return the public hook description
 */
struct upipe_http_src_hook *http_src_hook_init(struct http_src_hook *http,
                                               struct uref *flow_def);

#endif

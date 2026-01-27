/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short HTTP hooks for plain data read/write.
 */

#ifndef _UPIPE_MODULES_HTTP_SOURCE_HOOK_H_
#define _UPIPE_MODULES_HTTP_SOURCE_HOOK_H_

#include "upipe-modules/upipe_http_source.h"

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

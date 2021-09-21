/*
 * Copyright (C) 2021 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
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
 * @short declarations for a Upipe event loop using libsrt
 */

#ifndef _UPUMP_SRT_UPUMP_SRT_H_
/** @hidden */
#define _UPUMP_SRT_UPUMP_SRT_H_

#include "upipe/upump.h"

#include <srt/srt.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPUMP_SRT_SIGNATURE UBASE_FOURCC('s','r','t',' ')

/** @This extends upump_type with specific commands for upump_srt. */
enum upump_srt_type {
    UPUMP_SRT_TYPE_SENTINEL = UPUMP_TYPE_LOCAL,

    /** event triggers on readable srt socket (int) */
    UPUMP_SRT_TYPE_READ,
    /** event triggers on writable srt socket (int) */
    UPUMP_SRT_TYPE_WRITE,
};

/** @This allocates and initializes a upump_mgr structure.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_srt_mgr_alloc(uint16_t upump_pool_depth,
                                      uint16_t upump_blocker_pool_depth);

/** @This allocates and initializes a pump for a readable srt socket.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @param refcount pointer to urefcount structure to increment during callback,
 * or NULL
 * @param socket srt socket to watch
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_srt_alloc_read(struct upump_mgr *mgr,
                                                 upump_cb cb, void *opaque,
                                                 struct urefcount *refcount,
                                                 SRTSOCKET socket)
{
    return upump_alloc(mgr, cb, opaque, refcount, UPUMP_SRT_TYPE_READ,
                       UPUMP_SRT_SIGNATURE, socket);
}

/** @This allocates and initializes a pump for a writable srt socket.
 *
 * @param mgr management structure for this event loop
 * @param cb function to call when the pump triggers
 * @param opaque pointer to the module's internal structure
 * @param refcount pointer to urefcount structure to increment during callback,
 * or NULL
 * @param socket srt socket to watch
 * @return pointer to allocated pump, or NULL in case of failure
 */
static inline struct upump *upump_srt_alloc_write(struct upump_mgr *mgr,
                                                  upump_cb cb, void *opaque,
                                                  struct urefcount *refcount,
                                                  SRTSOCKET socket)
{
    return upump_alloc(mgr, cb, opaque, refcount, UPUMP_SRT_TYPE_WRITE,
                       UPUMP_SRT_SIGNATURE, socket);
}

#ifdef __cplusplus
}
#endif
#endif

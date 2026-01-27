/*
 * Copyright (C) 2021 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
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

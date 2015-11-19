/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 * @short Upipe exclusive access to non-reentrant resource
 * Primitives in this file allow to run a call-back when an exclusive access
 * to a non-reentrant resource is granted, in an asynchronous, upump-aware
 * way.
 */

#ifndef _UPIPE_UDEAL_H_
/** @hidden */
#define _UPIPE_UDEAL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/config.h>
#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/ueventfd.h>
#include <upipe/upump.h>

#include <assert.h>

/** @This is the implementation of a structure that deals access to a
 * non-reentrant resource. */
struct udeal {
    /** number of waiters */
    uatomic_uint32_t waiters;
    /** number of accesses to the resource (0 or 1) */
    uatomic_uint32_t access;
    /** ueventfd triggered when a waiter may be unblocked */
    struct ueventfd event;
};

/** @This initializes a udeal.
 *
 * @param udeal pointer to a udeal structure
 * @return false in case of failure
 */
static inline bool udeal_init(struct udeal *udeal)
{
    if (unlikely(!ueventfd_init(&udeal->event, true)))
        return false;

    uatomic_init(&udeal->waiters, 0);
    uatomic_init(&udeal->access, 0);
    return true;
}

/** @This allocates a watcher triggering when a waiter may be unblocked.
 *
 * @param udeal pointer to a udeal structure
 * @param upump_mgr management structure for this event loop
 * @param cb function to call when the watcher triggers
 * @param opaque pointer to the module's internal structure
 * @param refcount pointer to urefcount structure to increment during callback,
 * or NULL
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static inline struct upump *udeal_upump_alloc(struct udeal *udeal,
                                              struct upump_mgr *upump_mgr,
                                              upump_cb cb, void *opaque,
                                              struct urefcount *refcount)
{
    return ueventfd_upump_alloc(&udeal->event, upump_mgr, cb, opaque,
                                refcount);
}

/** @This starts the watcher and tries to immediately run the call-back.
 *
 * @param udeal pointer to a udeal structure
 * @param upump watcher allocated by @ref udeal_upump_alloc
 */
static inline void udeal_start(struct udeal *udeal, struct upump *upump)
{
    upump_start(upump);
    uint32_t waiters_before = uatomic_fetch_add(&udeal->waiters, 1);
#ifndef UDEAL_DEBUG
    if (likely(waiters_before == 0))
        upump->cb(upump);
#endif
}

/** @This tries to grab the resource.
 *
 * @param udeal pointer to a udeal structure
 * @return true if the resource may be exclusively used
 */
static inline bool udeal_grab(struct udeal *udeal)
{
    while (unlikely(uatomic_fetch_add(&udeal->access, 1) > 0)) {
        ueventfd_read(&udeal->event);

        /* double-check */
        if (likely(uatomic_fetch_sub(&udeal->access, 1) > 1))
            return false;

        /* try again */
        ueventfd_write(&udeal->event);
    }

    return true;
}

/** @This yields access to an exclusive resource previously acquired from
 * @ref udeal_grab, and stops the watcher.
 *
 * @param udeal pointer to a udeal structure
 * @param upump watcher allocated by @ref udeal_upump_alloc
 */
static inline void udeal_yield(struct udeal *udeal, struct upump *upump)
{
    uatomic_fetch_sub(&udeal->access, 1);
    if (uatomic_fetch_sub(&udeal->waiters, 1) > 1)
        ueventfd_write(&udeal->event);
    upump_stop(upump);
}

/** @This aborts the watcher before it has had a chance to run. It must only
 * be called in case of abort, otherwise @ref udeal_yield does the same job.
 *
 * @param udeal pointer to a udeal structure
 * @param upump watcher allocated by @ref udeal_upump_alloc
 * @return false in case of upump error
 */
static inline void udeal_abort(struct udeal *udeal, struct upump *upump)
{
    uatomic_fetch_sub(&udeal->waiters, 1);
    upump_stop(upump);
}

/** @This cleans up the udeal structure.
 *
 * @param udeal pointer to a udeal structure
 */
static inline void udeal_clean(struct udeal *udeal)
{
    uatomic_clean(&udeal->waiters);
    uatomic_clean(&udeal->access);
    ueventfd_clean(&udeal->event);
}

#ifdef __cplusplus
}
#endif
#endif

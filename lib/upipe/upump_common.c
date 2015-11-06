/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short common functions for event loop handlers
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/upool.h>
#include <upipe/upump_common.h>
#include <upipe/upump_blocker.h>

#include <stdlib.h>

/** @This stores extra opaque structures for blockers.
 */
struct upump_blocker_common {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** exported structure */
    struct upump_blocker blocker;
};

UBASE_FROM_TO(upump_blocker_common, upump_blocker, upump_blocker, blocker)
UBASE_FROM_TO(upump_blocker_common, uchain, uchain, uchain)

/** @This allocates and initializes a blocker.
 *
 * @param upump description structure of the pump
 * @return pointer to blocker
 */
struct upump_blocker *upump_common_blocker_alloc(struct upump *upump)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_mgr(upump->mgr);
    struct upump_blocker_common *blocker_common =
        upool_alloc(&common_mgr->upump_blocker_pool,
                    struct upump_blocker_common *);
    if (unlikely(blocker_common == NULL))
        return NULL;
    uchain_init(&blocker_common->uchain);

    struct upump_blocker *blocker =
        upump_blocker_common_to_upump_blocker(blocker_common);

    struct upump_common *common = upump_common_from_upump(upump);
    bool was_blocked = !ulist_empty(&common->blockers);
    ulist_add(&common->blockers,
              upump_blocker_common_to_uchain(blocker_common));
    if (common->started && !was_blocked) {
        struct upump_common_mgr *common_mgr =
            upump_common_mgr_from_upump_mgr(upump->mgr);
        common_mgr->upump_real_stop(upump);
    }
    return blocker;
}

/** @This releases a blocker, and if allowed restarts the pump.
 *
 * @param blocker description structure of the blocker
 */
void upump_common_blocker_free(struct upump_blocker *blocker)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_mgr(blocker->upump->mgr);
    struct upump_blocker_common *blocker_common =
        upump_blocker_common_from_upump_blocker(blocker);
    struct upump_common *common = upump_common_from_upump(blocker->upump);

    ulist_delete(upump_blocker_common_to_uchain(blocker_common));
    if (common->started && ulist_empty(&common->blockers)) {
        struct upump_common_mgr *common_mgr =
            upump_common_mgr_from_upump_mgr(blocker->upump->mgr);
        common_mgr->upump_real_start(blocker->upump);
    }

    upool_free(&common_mgr->upump_blocker_pool, blocker_common);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to upump_common_blocker or NULL in case of allocation error
 */
static void *upump_common_blocker_alloc_inner(struct upool *upool)
{
    return malloc(sizeof(struct upump_blocker_common));
}

/** @This frees a blocker structure.
 *
 * @param upool pointer to upool
 * @param blocker_common pointer to block_common to free
 */
void upump_common_blocker_free_inner(struct upool *upool, void *blocker_common)
{
    free(blocker_common);
}

/** @This initializes the common part of a pump.
 *
 * @param upump description structure of the pump
 */
void upump_common_init(struct upump *upump)
{
    struct upump_common *common = upump_common_from_upump(upump);
    common->started = false;
    ulist_init(&common->blockers);
}

/** @This dispatches a pump.
 *
 * @param upump description structure of the pump
 */
void upump_common_dispatch(struct upump *upump)
{
    struct urefcount *refcount =
        upump->refcount != NULL && !urefcount_dead(upump->refcount) ?
        upump->refcount : NULL;
    urefcount_use(refcount);
    upump->cb(upump);
    urefcount_release(refcount);
}

/** @This starts a pump if allowed.
 *
 * @param upump description structure of the pump
 */
void upump_common_start(struct upump *upump)
{
    struct upump_common *common = upump_common_from_upump(upump);
    common->started = true;
    if (ulist_empty(&common->blockers)) {
        struct upump_common_mgr *common_mgr =
            upump_common_mgr_from_upump_mgr(upump->mgr);
        common_mgr->upump_real_start(upump);
    }
}

/** @This stops a pump if needed.
 *
 * @param upump description structure of the pump
 */
void upump_common_stop(struct upump *upump)
{
    struct upump_common *common = upump_common_from_upump(upump);
    common->started = false;
    if (ulist_empty(&common->blockers)) {
        struct upump_common_mgr *common_mgr =
            upump_common_mgr_from_upump_mgr(upump->mgr);
        common_mgr->upump_real_stop(upump);
    }
}

/** @This cleans up the common part of a pump.
 *
 * @param upump description structure of the pump
 */
void upump_common_clean(struct upump *upump)
{
    struct upump_common *common = upump_common_from_upump(upump);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&common->blockers, uchain, uchain_tmp) {
        struct upump_blocker_common *blocker_common =
            upump_blocker_common_from_uchain(uchain);
        struct upump_blocker *blocker =
            upump_blocker_common_to_upump_blocker(blocker_common);
        struct urefcount *refcount =
            upump->refcount != NULL && !urefcount_dead(upump->refcount) ?
            upump->refcount : NULL;
        urefcount_use(refcount);
        blocker->cb(blocker);
        urefcount_release(refcount);
    }
}

/** @This returns the extra buffer space needed for pools.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 */
size_t upump_common_mgr_sizeof(uint16_t upump_pool_depth,
                               uint16_t upump_blocker_pool_depth)
{
    return upool_sizeof(upump_pool_depth) +
           upool_sizeof(upump_blocker_pool_depth);
}

/** @This instructs an existing manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_vacuum(struct upump_mgr *mgr)
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    upool_vacuum(&common_mgr->upump_pool);
    upool_vacuum(&common_mgr->upump_blocker_pool);
}

/** @This cleans up the common parts of a upump_common_mgr structure.
 * Note that all pumps have to be stopped before.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_clean(struct upump_mgr *mgr)
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    upool_clean(&common_mgr->upump_pool);
    upool_clean(&common_mgr->upump_blocker_pool);
}

/** @This initializes the common parts of a upump_common_mgr structure.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param pool_extra extra buffer space (see @ref upump_common_mgr_sizeof)
 * @param upump_real_start function of the real manager that starts a pump
 * @param upump_real_stop function of the real manager that stops a pump
 * @param upump_alloc_inner function to call to allocate a upump buffer
 * @param upump_free_inner function to call to release a upump buffer
 */
void upump_common_mgr_init(struct upump_mgr *mgr,
                           uint16_t upump_pool_depth,
                           uint16_t upump_blocker_pool_depth, void *pool_extra,
                           void (*upump_real_start)(struct upump *),
                           void (*upump_real_stop)(struct upump *),
                           void *(*upump_alloc_inner)(struct upool *),
                           void (*upump_free_inner)(struct upool *, void *))
{
    uchain_init(&mgr->uchain);
    mgr->opaque = NULL;
    mgr->upump_start = upump_common_start;
    mgr->upump_stop = upump_common_stop;
    mgr->upump_blocker_alloc = upump_common_blocker_alloc;
    mgr->upump_blocker_free = upump_common_blocker_free;

    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    common_mgr->upump_real_start = upump_real_start;
    common_mgr->upump_real_stop = upump_real_stop;

    upool_init(&common_mgr->upump_pool, upump_pool_depth, pool_extra,
               upump_alloc_inner, upump_free_inner);
    upool_init(&common_mgr->upump_blocker_pool, upump_blocker_pool_depth,
               pool_extra + upool_sizeof(upump_pool_depth),
               upump_common_blocker_alloc_inner,
               upump_common_blocker_free_inner);
}

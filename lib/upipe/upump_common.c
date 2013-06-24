/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
#include <upipe/ulifo.h>
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

/** @This returns the high-level upump_blocker structure.
 *
 * @param common pointer to the upump_blocker_common structure
 * @return pointer to the upump_blocker structure
 */
static inline struct upump_blocker *
    upump_blocker_common_to_upump_blocker(struct upump_blocker_common *common)
{
    return &common->blocker;
}

/** @This returns the private upump_blocker_common structure.
 *
 * @param blocker pointer to the upump_blocker structure
 * @return pointer to the upump_blocker_common structure
 */
static inline struct upump_blocker_common *
    upump_blocker_common_from_upump_blocker(struct upump_blocker *blocker)
{
    return container_of(blocker, struct upump_blocker_common, blocker);
}

/** @This returns the high-level upump_blocker_common structure.
 *
 * @param uchain pointer to the uchain structure wrapped into the
 * upump_blocker_common
 * @return pointer to the upump_blocker_common structure
 */
static inline struct upump_blocker_common *
    upump_blocker_common_from_uchain(struct uchain *uchain)
{
    return container_of(uchain, struct upump_blocker_common, uchain);
}

/** @This returns the uchain structure used for FIFO, LIFO and lists.
 *
 * @param upump_blocker_common upump_blocker_common structure
 * @return pointer to the uchain structure
 */
static inline struct uchain *
    upump_blocker_common_to_uchain(struct upump_blocker_common *common)
{
    return &common->uchain;
}

/** @This allocates and initializes a blocker.
 *
 * @param upump description structure of the pump
 * @return pointer to blocker
 */
struct upump_blocker *upump_common_blocker_alloc(struct upump *upump)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_mgr(upump->mgr);
    struct upump_blocker *blocker = ulifo_pop(&common_mgr->upump_blocker_pool,
                                              struct upump_blocker *);
    struct upump_blocker_common *blocker_common;
    if (unlikely(blocker == NULL)) {
        blocker_common = malloc(sizeof(struct upump_blocker_common));
        if (unlikely(blocker_common == NULL))
            return NULL;
        blocker = upump_blocker_common_to_upump_blocker(blocker_common);
    } else
        blocker_common = upump_blocker_common_from_upump_blocker(blocker);

    uchain_init(&blocker_common->uchain);

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

/** @This frees a blocker structure.
 *
 * @param blocker description structure of the blocker
 */
void upump_common_blocker_free_inner(struct upump_blocker *blocker)
{
    struct upump_blocker_common *blocker_common =
        upump_blocker_common_from_upump_blocker(blocker);
    free(blocker_common);
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

    ulist_remove(&common->blockers,
                 upump_blocker_common_to_uchain(blocker_common));
    if (common->started && ulist_empty(&common->blockers)) {
        struct upump_common_mgr *common_mgr =
            upump_common_mgr_from_upump_mgr(blocker->upump->mgr);
        common_mgr->upump_real_start(blocker->upump);
    }

    if (unlikely(!ulifo_push(&common_mgr->upump_blocker_pool, blocker)))
        upump_common_blocker_free_inner(blocker);
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
    upump->cb(upump);
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
    struct uchain *uchain;
    ulist_delete_foreach (&common->blockers, uchain) {
        struct upump_blocker_common *blocker_common =
            upump_blocker_common_from_uchain(uchain);
        struct upump_blocker *blocker =
            upump_blocker_common_to_upump_blocker(blocker_common);
        blocker->cb(blocker);
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
    return ulifo_sizeof(upump_pool_depth) +
           ulifo_sizeof(upump_blocker_pool_depth);
}

/** @This instructs an existing manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 * @param upump_free_inner function to call to release a upump buffer
 */
void upump_common_mgr_vacuum(struct upump_mgr *mgr,
                             void (*upump_free_inner)(struct upump *))
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    struct upump *upump;
    struct upump_blocker *blocker;

    while ((upump = ulifo_pop(&common_mgr->upump_pool, struct upump *)) != NULL)
        upump_free_inner(upump);
    while ((blocker = ulifo_pop(&common_mgr->upump_blocker_pool,
                               struct upump_blocker *)) != NULL)
        upump_common_blocker_free_inner(blocker);
}

/** @This cleans up the common parts of a upump_common_mgr structure.
 * Note that all pumps have to be stopped before.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 * @param upump_free_inner function to call to release a upump buffer
 */
void upump_common_mgr_clean(struct upump_mgr *mgr,
                            void (*upump_free_inner)(struct upump *))
{
    upump_common_mgr_vacuum(mgr, upump_free_inner);
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    ulifo_clean(&common_mgr->upump_pool);
    ulifo_clean(&common_mgr->upump_blocker_pool);
    urefcount_clean(&mgr->refcount);
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
 */
void upump_common_mgr_init(struct upump_mgr *mgr,
                           uint16_t upump_pool_depth,
                           uint16_t upump_blocker_pool_depth, void *pool_extra,
                           void (*upump_real_start)(struct upump *),
                           void (*upump_real_stop)(struct upump *))
{
    urefcount_init(&mgr->refcount);
    mgr->upump_start = upump_common_start;
    mgr->upump_stop = upump_common_stop;
    mgr->upump_blocker_alloc = upump_common_blocker_alloc;
    mgr->upump_blocker_free = upump_common_blocker_free;

    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    common_mgr->upump_real_start = upump_real_start;
    common_mgr->upump_real_stop = upump_real_stop;

    ulifo_init(&common_mgr->upump_pool, upump_pool_depth, pool_extra);
    ulifo_init(&common_mgr->upump_blocker_pool, upump_blocker_pool_depth,
               pool_extra + ulifo_sizeof(upump_pool_depth));
}

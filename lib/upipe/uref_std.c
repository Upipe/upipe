/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe uref standard manager
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulifo.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>

#include <stdlib.h>
#include <assert.h>

/** @This is a super-set of the uref_mgr structure with additional local
 * members */
struct uref_std_mgr {
    /** uref pool */
    struct ulifo uref_pool;

    /** refcount management structure */
    urefcount refcount;
    /** common management structure */
    struct uref_mgr mgr;
};

/** @internal @This returns the high-level uref_mgr structure.
 *
 * @param std_mgr pointer to the uref_std_mgr structure
 * @return pointer to the uref_mgr structure
 */
static inline struct uref_mgr *uref_std_mgr_to_uref_mgr(struct uref_std_mgr *std_mgr)
{
    return &std_mgr->mgr;
}

/** @internal @This returns the private uref_std_mgr structure.
 *
 * @param mgr description structure of the uref mgr
 * @return pointer to the uref_std_mgr structure
 */
static inline struct uref_std_mgr *uref_std_mgr_from_uref_mgr(struct uref_mgr *mgr)
{
    return container_of(mgr, struct uref_std_mgr, mgr);
}

/** @This allocates a uref.
 *
 * @param mgr common management structure
 * @return pointer to uref or NULL in case of allocation error
 */
static struct uref *uref_std_alloc(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    struct uref *uref = ulifo_pop(&std_mgr->uref_pool, struct uref *);
    if (uref == NULL) {
        uref = malloc(sizeof(struct uref));
        if (unlikely(uref == NULL))
            return NULL;

        uref->mgr = mgr;
    }
    uchain_init(&uref->uchain);

    uref_mgr_use(mgr);
    return uref;
}

/** @internal @This frees a uref and all associated data structures.
 *
 * @param uref pointer to uref structure to free
 */
static void uref_std_free_inner(struct uref *uref)
{
    free(uref);
}

/** @This recycles or frees a uref.
 *
 * @param uref pointer to a uref structure
 */
static void uref_std_free(struct uref *uref)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(uref->mgr);
    if (unlikely(!ulifo_push(&std_mgr->uref_pool, uref)))
        uref_std_free_inner(uref);

    uref_mgr_release(&std_mgr->mgr);
}

/** @This instructs an existing uref standard manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a uref manager
 */
static void uref_std_mgr_vacuum(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    struct uref *uref;

    while ((uref = ulifo_pop(&std_mgr->uref_pool, struct uref *)) != NULL)
        uref_std_free_inner(uref);
}

/** @This increments the reference count of a uref manager.
 *
 * @param mgr pointer to uref manager
 */
static void uref_std_mgr_use(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    urefcount_use(&std_mgr->refcount);
}

/** @This decrements the reference count of a uref manager or frees it.
 *
 * @param mgr pointer to a uref manager
 */
static void uref_std_mgr_release(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    if (unlikely(urefcount_release(&std_mgr->refcount))) {
        uref_std_mgr_vacuum(mgr);
        ulifo_clean(&std_mgr->uref_pool);
        udict_mgr_release(mgr->udict_mgr);

        urefcount_clean(&std_mgr->refcount);
        free(std_mgr);
    }
}

/** @This allocates a new instance of the standard uref manager
 *
 * @param uref_pool_depth maximum number of uref structures in the pool
 * @param udict_mgr udict manager to use to allocate udict structures
 * @param control_attr_size extra attributes space for control packets
 * @return pointer to manager, or NULL in case of error
 */
struct uref_mgr *uref_std_mgr_alloc(uint16_t uref_pool_depth,
                                    struct udict_mgr *udict_mgr,
                                    int control_attr_size)
{
    assert(udict_mgr != NULL);
    assert(control_attr_size >= 0);

    struct uref_std_mgr *std_mgr = malloc(sizeof(struct uref_std_mgr) +
                                          ulifo_sizeof(uref_pool_depth));
    if (unlikely(std_mgr == NULL))
        return NULL;

    ulifo_init(&std_mgr->uref_pool, uref_pool_depth,
               (void *)std_mgr + sizeof(struct uref_std_mgr));

    std_mgr->mgr.control_attr_size = control_attr_size;
    std_mgr->mgr.udict_mgr = udict_mgr;
    udict_mgr_use(udict_mgr);

    urefcount_init(&std_mgr->refcount);
    std_mgr->mgr.uref_alloc = uref_std_alloc;
    std_mgr->mgr.uref_free = uref_std_free;
    std_mgr->mgr.uref_mgr_vacuum = uref_std_mgr_vacuum;
    std_mgr->mgr.uref_mgr_use = uref_std_mgr_use;
    std_mgr->mgr.uref_mgr_release = uref_std_mgr_release;
    
    return uref_std_mgr_to_uref_mgr(std_mgr);
}

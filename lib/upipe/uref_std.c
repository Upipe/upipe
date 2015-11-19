/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
#include <upipe/upool.h>
#include <upipe/udict.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>

#include <stdlib.h>
#include <assert.h>

/** @This is a super-set of the uref_mgr structure with additional local
 * members */
struct uref_std_mgr {
    /** refcount management structure */
    struct urefcount urefcount;
    /** uref pool */
    struct upool uref_pool;

    /** common management structure */
    struct uref_mgr mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(uref_std_mgr, uref_mgr, uref_mgr, mgr)
UBASE_FROM_TO(uref_std_mgr, urefcount, urefcount, urefcount)
UBASE_FROM_TO(uref_std_mgr, upool, uref_pool, uref_pool)

/** @This allocates a uref.
 *
 * @param mgr common management structure
 * @return pointer to uref or NULL in case of allocation error
 */
static struct uref *uref_std_alloc(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    struct uref *uref = upool_alloc(&std_mgr->uref_pool, struct uref *);
    if (unlikely(uref == NULL))
        return NULL;
    uchain_init(&uref->uchain);
    uref_mgr_use(mgr);
    return uref;
}

/** @This recycles or frees a uref.
 *
 * @param uref pointer to a uref structure
 */
static void uref_std_free(struct uref *uref)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(uref->mgr);
    upool_free(&std_mgr->uref_pool, uref);
    uref_mgr_release(&std_mgr->mgr);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to uref or NULL in case of allocation error
 */
static void *uref_std_alloc_inner(struct upool *upool)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_pool(upool);
    struct uref *uref = malloc(sizeof(struct uref));
    if (unlikely(uref == NULL))
        return NULL;
    uref->mgr = uref_std_mgr_to_uref_mgr(std_mgr);
    return uref;
}

/** @internal @This frees a buffer.
 *
 * @param upool pointer to upool
 * @param uref pointer to shared structure to free
 */
static void uref_std_free_inner(struct upool *upool, void *uref)
{
    free(uref);
}

/** @internal @This instructs an existing uref standard manager to release all
 * structures currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a uref manager
 */
static void uref_std_mgr_vacuum(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    upool_vacuum(&std_mgr->uref_pool);
}

/** @This processes control commands on a uref_std_mgr.
 *
 * @param mgr pointer to a uref_mgr structure
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int uref_std_mgr_control(struct uref_mgr *mgr, int command, va_list args)
{
    switch (command) {
        case UREF_MGR_VACUUM:
            uref_std_mgr_vacuum(mgr);
            return UBASE_ERR_NONE;
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a uref manager.
 *
 * @param urefcount pointer to a urefcount
 */
static void uref_std_mgr_free(struct urefcount *urefcount)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_urefcount(urefcount);
    struct uref_mgr *mgr = uref_std_mgr_to_uref_mgr(std_mgr);
    upool_clean(&std_mgr->uref_pool);
    udict_mgr_release(mgr->udict_mgr);

    urefcount_clean(urefcount);
    free(std_mgr);
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
                                          upool_sizeof(uref_pool_depth));
    if (unlikely(std_mgr == NULL))
        return NULL;

    upool_init(&std_mgr->uref_pool, uref_pool_depth, std_mgr->upool_extra,
               uref_std_alloc_inner, uref_std_free_inner);

    std_mgr->mgr.control_attr_size = control_attr_size;
    std_mgr->mgr.udict_mgr = udict_mgr;
    udict_mgr_use(udict_mgr);

    urefcount_init(uref_std_mgr_to_urefcount(std_mgr), uref_std_mgr_free);
    std_mgr->mgr.refcount = uref_std_mgr_to_urefcount(std_mgr);
    std_mgr->mgr.uref_alloc = uref_std_alloc;
    std_mgr->mgr.uref_free = uref_std_free;
    std_mgr->mgr.uref_mgr_control = uref_std_mgr_control;
    
    return uref_std_mgr_to_uref_mgr(std_mgr);
}

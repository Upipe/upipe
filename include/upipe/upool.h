/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe pool of buffers, based on @ref ulifo
 */

#ifndef _UPIPE_UPOOL_H_
/** @hidden */
#define _UPIPE_UPOOL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulifo.h>

/** @hidden */
struct upool;

/** @This is a call-back to allocate new elements */
typedef void *(*upool_alloc_cb)(struct upool *);
/** @This is a call-back to release unused elements */
typedef void (*upool_free_cb)(struct upool *, void *);

/** @This is the implementation of a pool of buffers. */
struct upool {
    /** pointer to refcount management structure */
    struct urefcount *refcount;
    /** lifo */
    struct ulifo lifo;
    /** call-back to allocate new elements */
    upool_alloc_cb alloc_cb;
    /** call-back to release unused elements */
    upool_free_cb free_cb;
};

/** @This returns the required size of extra data space for upool.
 *
 * @param length maximum number of elements in the LIFO
 * @return size in octets to allocate
 */
#define upool_sizeof(length) ulifo_sizeof(length)

/** @This initializes a upool.
 *
 * @param upool pointer to a upool structure
 * @param refcount pointer to refcount management structure
 * @param length maximum number of elements in the pool
 * @param extra mandatory extra space allocated by the caller, with the size
 * returned by @ref #upool_sizeof
 */
static inline void upool_init(struct upool *upool, struct urefcount *refcount,
                              uint16_t length, void *extra,
                              upool_alloc_cb alloc_cb, upool_free_cb free_cb)
{
    upool->refcount = refcount;
    ulifo_init(&upool->lifo, length, extra);
    upool->alloc_cb = alloc_cb;
    upool->free_cb = free_cb;
}

/** @This increments the reference count of a upool.
 *
 * @param upool pointer to upool
 * @return same pointer to upool
 */
static inline struct upool *upool_use(struct upool *upool)
{
    if (upool == NULL)
        return NULL;
    urefcount_use(upool->refcount);
    return upool;
}

/** @This decrements the reference count of a upool or frees it.
 *
 * @param upool pointer to upool
 */
static inline void upool_release(struct upool *upool)
{
    if (upool != NULL)
        urefcount_release(upool->refcount);
}

/** @internal @This allocates an elements from the upool.
 *
 * @param upool pointer to a upool structure
 * @return allocated element, or NULL in case of allocation error
 */
static inline void *upool_alloc_internal(struct upool *upool)
{
    void *obj = ulifo_pop(&upool->lifo, void *);
    if (unlikely(obj == NULL))
        obj = upool->alloc_cb(upool);
    if (obj != NULL)
        upool_use(upool);
    return obj;
}

/** @This allocates an elements from the upool.
 *
 * @param upool pointer to a upool structure
 * @param type type of the opaque pointer
 * @return allocated element, or NULL in case of allocation error
 */
#define upool_alloc(upool, type) (type)upool_alloc_internal(upool)

/** @This frees an element.
 *
 * @param upool pointer to a upool structure
 * @param obj element to free
 */
static inline void upool_free(struct upool *upool, void *obj)
{
    if (unlikely(!ulifo_push(&upool->lifo, obj)))
        upool->free_cb(upool, obj);
    upool_release(upool);
}

/** @This empties a upool.
 *
 * @param upool pointer to a upool structure
 */
static inline void upool_vacuum(struct upool *upool)
{
    void *obj;
    while ((obj = ulifo_pop(&upool->lifo, void *)) != NULL)
        upool->free_cb(upool, obj);
}

/** @This empties and cleans up a upool.
 *
 * @param upool pointer to a upool structure
 */
static inline void upool_clean(struct upool *upool)
{
    upool_vacuum(upool);
    ulifo_clean(&upool->lifo);
}

#ifdef __cplusplus
}
#endif
#endif

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
 * @short Upipe generic memory allocators
 */

#ifndef _UPIPE_UMEM_H_
/** @hidden */
#define _UPIPE_UMEM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/urefcount.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/** @hidden */
struct umem_mgr;

/** @This is not treated the same way as other structures in Upipe:
 * it is not allocated by the manager, but by the caller. The manager only
 * "inits" it. */
struct umem {
    /** pointer to the entity responsible for the management */
    struct umem_mgr *mgr;
    /** pointer to actual buffer space */
    uint8_t *buffer;
    /** allocated size of the buffer space */
    size_t size;
};

/** @This returns a pointer to the buffer space pointed to by a umem.
 *
 * @param umem pointer to umem
 * @return pointer to the buffer space
 */
static inline uint8_t *umem_buffer(struct umem *umem)
{
    assert(umem != NULL);
    return umem->buffer;
}

/** @This returns the size of the buffer space pointed to by a umem.
 *
 * @param umem pointer to umem
 * @return size of the buffer space
 */
static inline size_t umem_size(struct umem *umem)
{
    assert(umem != NULL);
    return umem->size;
}

/** @This defines a memory allocator management structure.
 */
struct umem_mgr {
    /** pointer to refcount management structure */
    struct urefcount *refcount;

    /** function to allocate a new memory block */
    bool (*umem_alloc)(struct umem_mgr *, struct umem *, size_t);
    /** function to resize umem */
    bool (*umem_realloc)(struct umem *, size_t);
    /** function to free a umem */
    void (*umem_free)(struct umem *);

    /** function to release all buffers kept in pools */
    void (*umem_mgr_vacuum)(struct umem_mgr *);
};

/** @This allocates a new umem buffer space.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, filled in with the required pointer
 * and size (previous content is discarded)
 * @param size requested size of the umem
 * @return false if the memory couldn't be allocated (umem left untouched)
 */
static inline bool umem_alloc(struct umem_mgr *mgr, struct umem *umem,
                              size_t size)
{
    assert(umem != NULL);
    return mgr->umem_alloc(mgr, umem, size);
}

/** @This resizes a umem.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, previously successfully passed to
 * @ref umem_alloc, and filled in with the new pointer and size
 * @param new_size new requested size of the umem
 * @return false if the memory couldn't be allocated (umem left untouched)
 */
static inline bool umem_realloc(struct umem *umem, size_t new_size)
{
    assert(umem != NULL);
    return umem->mgr->umem_realloc(umem, new_size);
}

/** @This frees a umem.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, previously successfully passed to
 * @ref umem_alloc
 * @param umem pointer to umem
 */
static inline void umem_free(struct umem *umem)
{
    assert(umem != NULL);
    umem->mgr->umem_free(umem);
}

/** @This instructs an existing umem manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to umem manager
 */
static inline void umem_mgr_vacuum(struct umem_mgr *mgr)
{
    if (likely(mgr->umem_mgr_vacuum != NULL))
        mgr->umem_mgr_vacuum(mgr);
}

/** @This increments the reference count of a umem manager.
 *
 * @param mgr pointer to umem manager
 */
static inline void umem_mgr_use(struct umem_mgr *mgr)
{
    urefcount_use(mgr->refcount);
}

/** @This decrements the reference count of a umem manager or frees it.
 *
 * @param mgr pointer to umem manager
 */
static inline void umem_mgr_release(struct umem_mgr *mgr)
{
    urefcount_release(mgr->refcount);
}

#ifdef __cplusplus
}
#endif
#endif

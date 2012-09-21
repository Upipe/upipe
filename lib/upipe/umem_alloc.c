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

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>

#include <stdlib.h>
#include <stdbool.h>

/** @This defines the private data structures of the umem alloc manager. */
struct umem_alloc_mgr {
    /** refcount management structure */
    urefcount refcount;
    /** common management structure */
    struct umem_mgr mgr;
};

/** @internal @This returns the high-level umem_mgr structure.
 *
 * @param alloc_mgr pointer to the umem_alloc_mgr structure
 * @return pointer to the umem_mgr structure
 */
static inline struct umem_mgr *umem_alloc_mgr_to_umem_mgr(struct umem_alloc_mgr *alloc_mgr)
{
    return &alloc_mgr->mgr;
}

/** @internal @This returns the private umem_alloc_mgr structure.
 *
 * @param mgr description structure of the umem mgr
 * @return pointer to the umem_alloc_mgr structure
 */
static inline struct umem_alloc_mgr *umem_alloc_mgr_from_umem_mgr(struct umem_mgr *mgr)
{
    return container_of(mgr, struct umem_alloc_mgr, mgr);
}

/** @This allocates a new umem buffer space.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, filled in with the required pointer
 * and size (previous content is discarded)
 * @param size requested size of the umem
 * @return false if the memory couldn't be allocated (umem left untouched)
 */
static bool umem_alloc_alloc(struct umem_mgr *mgr, struct umem *umem,
                             size_t size)
{
    uint8_t *buffer = malloc(size);
    if (unlikely(buffer == NULL))
        return false;

    umem->buffer = buffer;
    umem->size = size;
    umem->mgr = mgr;
    return true;
}

/** @This resizes a umem.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, previously successfully passed to
 * @ref umem_alloc, and filled in with the new pointer and size
 * @param new_size new requested size of the umem
 * @return false if the memory couldn't be allocated (umem left untouched)
 */
static bool umem_alloc_realloc(struct umem *umem, size_t new_size)
{
    uint8_t *buffer = realloc(umem->buffer, new_size);
    if (unlikely(buffer == NULL))
        return false;

    umem->buffer = buffer;
    umem->size = new_size;
    return true;
}

/** @This frees a umem.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, previously successfully passed to
 * @ref umem_alloc
 * @param umem pointer to umem
 */
static void umem_alloc_free(struct umem *umem)
{
    free(umem->buffer);
    umem->buffer = NULL;
    umem->mgr = NULL;
}

/** @This increments the reference count of a umem manager.
 *
 * @param mgr pointer to umem manager
 */
static void umem_alloc_mgr_use(struct umem_mgr *mgr)
{
    struct umem_alloc_mgr *alloc_mgr = umem_alloc_mgr_from_umem_mgr(mgr);
    urefcount_use(&alloc_mgr->refcount);
}

/** @This decrements the reference count of a umem manager or frees it.
 *
 * @param mgr pointer to umem manager
 */
static void umem_alloc_mgr_release(struct umem_mgr *mgr)
{
    struct umem_alloc_mgr *alloc_mgr = umem_alloc_mgr_from_umem_mgr(mgr);
    if (unlikely(urefcount_release(&alloc_mgr->refcount))) {
        urefcount_clean(&alloc_mgr->refcount);
        free(alloc_mgr);
    }
}

/** @This allocates a new instance of the umem alloc manager allocating buffers
 * from application memory directly with malloc()/free(), without any pool.
 *
 * @return pointer to manager, or NULL in case of error
 */
struct umem_mgr *umem_alloc_mgr_alloc(void)
{
    struct umem_alloc_mgr *alloc_mgr = malloc(sizeof(struct umem_alloc_mgr));
    if (unlikely(alloc_mgr == NULL))
        return NULL;

    urefcount_init(&alloc_mgr->refcount);
    alloc_mgr->mgr.umem_alloc = umem_alloc_alloc;
    alloc_mgr->mgr.umem_realloc = umem_alloc_realloc;
    alloc_mgr->mgr.umem_free = umem_alloc_free;
    alloc_mgr->mgr.umem_mgr_vacuum = NULL;
    alloc_mgr->mgr.umem_mgr_use = umem_alloc_mgr_use;
    alloc_mgr->mgr.umem_mgr_release = umem_alloc_mgr_release;

    return umem_alloc_mgr_to_umem_mgr(alloc_mgr);
}

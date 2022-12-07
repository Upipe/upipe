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

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>

#include <stdlib.h>
#include <stdbool.h>

/** @This defines the private data structures of the umem alloc manager. */
struct umem_alloc_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** common management structure */
    struct umem_mgr mgr;
};

UBASE_FROM_TO(umem_alloc_mgr, umem_mgr, umem_mgr, mgr)
UBASE_FROM_TO(umem_alloc_mgr, urefcount, urefcount, urefcount)

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
    ubase_clean_data(&umem->buffer);
    umem->mgr = NULL;
}

/** @This frees a umem manager.
 *
 * @param urefcont pointer to urefcount
 */
static void umem_alloc_mgr_free(struct urefcount *urefcount)
{
    struct umem_alloc_mgr *alloc_mgr = umem_alloc_mgr_from_urefcount(urefcount);
    urefcount_clean(urefcount);
    free(alloc_mgr);
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

    urefcount_init(umem_alloc_mgr_to_urefcount(alloc_mgr), umem_alloc_mgr_free);
    alloc_mgr->mgr.refcount = umem_alloc_mgr_to_urefcount(alloc_mgr);
    alloc_mgr->mgr.umem_alloc = umem_alloc_alloc;
    alloc_mgr->mgr.umem_realloc = umem_alloc_realloc;
    alloc_mgr->mgr.umem_free = umem_alloc_free;
    alloc_mgr->mgr.umem_mgr_vacuum = NULL;

    return umem_alloc_mgr_to_umem_mgr(alloc_mgr);
}

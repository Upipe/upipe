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
#include <upipe/ulifo.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

/** @This defines the private data structures of the umem pool manager. */
struct umem_pool_mgr {
    /** common management structure */
    struct umem_mgr mgr;

    /** size (in octets) of buffers of pools[0] */
    size_t pool0_size;
    /** number of pools of buffers */
    size_t nb_pools;
    /** buffer pools */
    struct ulifo pools[];
};

/** @internal @This returns the high-level umem_mgr structure.
 *
 * @param pool_mgr pointer to the umem_pool_mgr structure
 * @return pointer to the umem_mgr structure
 */
static inline struct umem_mgr *umem_pool_mgr_to_umem_mgr(struct umem_pool_mgr *pool_mgr)
{
    return &pool_mgr->mgr;
}

/** @internal @This returns the private umem_pool_mgr structure.
 *
 * @param mgr description structure of the umem mgr
 * @return pointer to the umem_pool_mgr structure
 */
static inline struct umem_pool_mgr *umem_pool_mgr_from_umem_mgr(struct umem_mgr *mgr)
{
    return container_of(mgr, struct umem_pool_mgr, mgr);
}

/** @internal @This returns the nearest bigger size to allocate for a umem of
 * the given size to fit into and returns the index of the appropriate pool.
 *
 * @param mgr description structure of the umem mgr
 * @param wanted desired size of the umem
 * @param real_p reference written with the actual size of the future buffer
 * @return index of the pool in which to find appropriate buffers
 */
static unsigned int umem_pool_find(struct umem_mgr *mgr, size_t wanted,
                                   size_t *real_p)
{
    struct umem_pool_mgr *pool_mgr = umem_pool_mgr_from_umem_mgr(mgr);
    size_t size = pool_mgr->pool0_size; 
    unsigned int pool;

    for (pool = 0; pool < pool_mgr->nb_pools; pool++)
        if (wanted <= (size << pool))
            break;
    if (likely(real_p != NULL))
        *real_p = pool < pool_mgr->nb_pools ? size << pool : wanted;
    return pool;
}

/** @This allocates a new umem buffer space.
 *
 * @param mgr management structure
 * @param umem caller-allocated structure, filled in with the required pointer
 * and size (previous content is discarded)
 * @param size requested size of the umem
 * @return false if the memory couldn't be allocated (umem left untouched)
 */
static bool umem_pool_alloc(struct umem_mgr *mgr, struct umem *umem,
                            size_t size)
{
    struct umem_pool_mgr *pool_mgr = umem_pool_mgr_from_umem_mgr(mgr);
    size_t real_size;
    unsigned int pool = umem_pool_find(mgr, size, &real_size);
    uint8_t *buffer = NULL;

    if (likely(pool < pool_mgr->nb_pools))
        buffer = ulifo_pop(&pool_mgr->pools[pool], uint8_t *);
    if (unlikely(buffer == NULL))
        buffer = malloc(real_size);
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
static bool umem_pool_realloc(struct umem *umem, size_t new_size)
{
    size_t real_old, real_new;
    umem_pool_find(umem->mgr, umem->size, &real_old);
    umem_pool_find(umem->mgr, new_size, &real_new);

    if (likely(real_old == real_new))
        return true;

    uint8_t *buffer = realloc(umem->buffer, real_new);
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
static void umem_pool_free(struct umem *umem)
{
    struct umem_pool_mgr *pool_mgr = umem_pool_mgr_from_umem_mgr(umem->mgr);
    unsigned int pool = umem_pool_find(umem->mgr, umem->size, NULL);

    if (unlikely(pool >= pool_mgr->nb_pools ||
                 !ulifo_push(&pool_mgr->pools[pool], umem->buffer)))
        free(umem->buffer);
    umem->buffer = NULL;
    umem->mgr = NULL;
}

/** @This instructs an existing umem manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to umem manager
 */
static void umem_pool_mgr_vacuum(struct umem_mgr *mgr)
{
    struct umem_pool_mgr *pool_mgr = umem_pool_mgr_from_umem_mgr(mgr);

    for (unsigned int i = 0; i < pool_mgr->nb_pools; i++) {
        uint8_t *buffer;
        while ((buffer = ulifo_pop(&pool_mgr->pools[i], uint8_t *)) != NULL)
            free(buffer);
    }
}

/** @This frees a umem manager.
 *
 * @param mgr pointer to umem manager
 */
static void umem_pool_mgr_free(struct umem_mgr *mgr)
{
    struct umem_pool_mgr *pool_mgr = umem_pool_mgr_from_umem_mgr(mgr);
    umem_pool_mgr_vacuum(mgr);

    for (unsigned int i = 0; i < pool_mgr->nb_pools; i++)
        ulifo_clean(&pool_mgr->pools[i]);

    urefcount_clean(&pool_mgr->mgr.refcount);
    free(pool_mgr);
}

/** @This allocates a new instance of the umem pool manager allocating buffers
 * from application memory, using pools in power of 2's.
 *
 * @param pool0_size size (in octets) of the smallest allocatable buffer; it
 * must be a power of 2
 * @param nb_pools number of buffer pools to maintain, with sizes in power of
 * 2's increments, followed, for each pool, by the maximum number of buffers
 * to keep in the pool (unsigned int); larger buffers will be directly managed
 * with malloc() and free()
 * @return pointer to manager, or NULL in case of error
 */
struct umem_mgr *umem_pool_mgr_alloc(size_t pool0_size, size_t nb_pools, ...)
{
    size_t alloc_size = sizeof(struct umem_pool_mgr) +
                        sizeof(struct ulifo) * nb_pools;
    unsigned int pools_depths[nb_pools];
    va_list args;
    va_start(args, nb_pools);
    for (unsigned int i = 0; i < nb_pools; i++) {
        pools_depths[i] = va_arg(args, unsigned int);
        assert(pools_depths[i] <= UINT16_MAX);
        alloc_size += ulifo_sizeof(pools_depths[i]);
    }
    va_end(args);

    struct umem_pool_mgr *pool_mgr = malloc(alloc_size);
    if (unlikely(pool_mgr == NULL))
        return NULL;

    pool_mgr->pool0_size = pool0_size;
    pool_mgr->nb_pools = nb_pools;

    void *extra = (void *)pool_mgr + sizeof(struct umem_pool_mgr) +
                  sizeof(struct ulifo) * nb_pools;

    for (unsigned int i = 0; i < nb_pools; i++) {
        ulifo_init(&pool_mgr->pools[i], pools_depths[i], extra);
        extra += ulifo_sizeof(pools_depths[i]);
    }

    urefcount_init(&pool_mgr->mgr.refcount);
    pool_mgr->mgr.umem_alloc = umem_pool_alloc;
    pool_mgr->mgr.umem_realloc = umem_pool_realloc;
    pool_mgr->mgr.umem_free = umem_pool_free;
    pool_mgr->mgr.umem_mgr_vacuum = umem_pool_mgr_vacuum;
    pool_mgr->mgr.umem_mgr_free = umem_pool_mgr_free;

    return umem_pool_mgr_to_umem_mgr(pool_mgr);
}

/** @This allocates a new instance of the umem pool manager allocating buffers
 * from application memory, using pools in power of 2's, with a simpler API.
 *
 * @param base_pools_depth number of buffers to keep in the pool for the smaller
 * buffers; for larger buffers the same number is used, divided by 2, 4, or 8
 * @return pointer to manager, or NULL in case of error
 */
struct umem_mgr *umem_pool_mgr_alloc_simple(uint16_t base_pools_depth)
{
    return umem_pool_mgr_alloc(32, 18,
                               base_pools_depth, /* 32 */
                               base_pools_depth, /* 64 */
                               base_pools_depth, /* 128 */
                               base_pools_depth, /* 256 */
                               base_pools_depth, /* 512 */
                               base_pools_depth, /* 1 Ki */
                               base_pools_depth, /* 2 Ki */
                               base_pools_depth, /* 4 Ki */
                               base_pools_depth / 2, /* 8 Ki */
                               base_pools_depth / 2, /* 16 Ki */
                               base_pools_depth / 2, /* 32 Ki */
                               base_pools_depth / 4, /* 64 Ki */
                               base_pools_depth / 4, /* 128 Ki */
                               base_pools_depth / 4, /* 256 Ki */
                               base_pools_depth / 4, /* 512 Ki */
                               base_pools_depth / 8, /* 1 Mi */
                               base_pools_depth / 8, /* 2 Mi */
                               base_pools_depth / 8); /* 4 Mi */
}

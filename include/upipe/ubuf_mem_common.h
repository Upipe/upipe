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
 * @short Upipe common functions for ubuf managers with umem storage
 */

#ifndef _UPIPE_UBUF_MEM_COMMON_H_
/** @hidden */
#define _UPIPE_UBUF_MEM_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/upool.h>
#include <upipe/umem.h>

#include <stdbool.h>

/** @This is the low-level shared structure with reference counting, pointing
 * to the actual data. */
struct ubuf_mem_shared {
    /** number of blocks pointing to the memory area */
    uatomic_uint32_t refcount;
    /** umem structure pointing to buffer */
    struct umem umem;
};

/** @This increments the reference count of a shared buffer.
 *
 * @param shared pointer to shared buffer
 * @return pointer to shared buffer
 */
static inline struct ubuf_mem_shared *
    ubuf_mem_shared_use(struct ubuf_mem_shared *shared)
{
    uatomic_fetch_add(&shared->refcount, 1);
    return shared;
}

/** @This decrements the reference count of a shared buffer.
 *
 * @param shared pointer to shared buffer
 * @return true if the buffer needs deallocation
 */
static inline bool ubuf_mem_shared_release(struct ubuf_mem_shared *shared)
{
    return uatomic_fetch_sub(&shared->refcount, 1) == 1;
}

/** @This checks whether there is only one reference to the shared buffer.
 *
 * @param shared pointer to shared buffer
 * @return true if there is only one reference
 */
static inline bool ubuf_mem_shared_single(struct ubuf_mem_shared *shared)
{
    return uatomic_load(&shared->refcount) == 1;
}

/** @This returns the shared buffer.
 *
 * @param shared pointer to shared buffer
 */
static inline uint8_t *ubuf_mem_shared_buffer(struct ubuf_mem_shared *shared)
{
    return umem_buffer(&shared->umem);
}

/** @This returns the size of the shared buffer.
 *
 * @param shared pointer to shared buffer
 */
static inline size_t ubuf_mem_shared_size(struct ubuf_mem_shared *shared)
{
    return umem_size(&shared->umem);
}

/** @This allocates the shared data structure.
 *
 * @param upool pointer to upool
 * @return pointer to ubuf_block_mem or NULL in case of allocation error
 */
void *ubuf_mem_shared_alloc_inner(struct upool *upool);

/** @This frees a shared data structure.
 *
 * @param upool pointer to upool
 * @param _shared pointer to shared structure to free
 */
void ubuf_mem_shared_free_inner(struct upool *upool, void *_shared);

/** @This declares eight functions dealing with the structure pools of
 * ubuf managers using umem storage.
 *
 * You must add two members to your private ubuf_mgr structure, for instance:
 * @code
 *  struct upool *ubuf_pool;
 *  struct upool *shared_pool
 * @end code
 *
 * And one member to your private ubuf structure, for instance:
 * @code
 *  struct ubuf_mem_shared *shared;
 * @end code
 *
 * Supposing the name of your structures are respectively ubuf_foo_mgr and
 * ubuf_foo, it declares:
 * @list
 * @item @code
 *  struct ubuf_foo *ubuf_foo_alloc_pool(struct ubuf_mgr *)
 * @end code
 * Allocates the data structure or fetches it from the pool.
 *
 * @item @code
 *  struct ubuf_mem_shared *ubuf_foo_shared_alloc_pool(struct ubuf_mgr *)
 * @end code
 * Allocates the shared data structure or fetches it from the pool.
 *
 * @item @code
 *  void ubuf_foo_free_pool(struct ubuf_mgr *, struct ubuf_foo *)
 * @end code
 * Deallocates the data structure or places it back into the pool.
 *
 * @item @code
 *  void ubuf_foo_shared_free_pool(struct ubuf_mgr *, struct ubuf_mem_shared *)
 * @end code
 * Deallocates the shared data structure or places it back into the pool.
 *
 * @item @code
 *  void ubuf_foo_mgr_vacuum_pool(struct ubuf_mgr *)
 * @end code
 * Releases all structures kept in pools.
 *
 * @item @code
 *  void ubuf_foo_mgr_clean_pool(struct ubuf_mgr *)
 * @end code
 * Called before deallocation of the manager.
 *
 * @item @code
 *  size_t ubuf_foo_mgr_sizeof_pool(uint16_t ubuf_pool_depth, uint16_t shared_pool_depth)
 * @end code
 * Returns the required size of extra data space for pools.
 *
 * @item @code
 *  void ubuf_foo_mgr_init_pool(struct ubuf_mgr *, uint16_t ubuf_pool_depth,
 *  uint16_t shared_pool_depth, void *extra, upool_alloc_cb ubuf_alloc_cb,
 *  upool_free_cb ubuf_free_cb)
 * @end code
 * Called when allocating the manager.
 * @end list
 *
 * You must also declare such as function prior to using this macro:
 * @code
 *  struct ubuf_foo_mgr *ubuf_foo_mgr_from_ubuf_mgr(struct ubuf_mgr *mgr)
 * @end code
 *
 * @param STRUCTURE name of your private ubuf structure
 * @param UBUF_POOL name of the ubuf pool in your private ubuf_mgr structure
 * @param SHARED_POOL name of the shared pool in your private ubuf_mgr structure
 * @param SHARED name of the @tt{struct ubuf_mem_shared} field of your private
 * ubuf structure
 */
#define UBUF_MEM_MGR_HELPER_POOL(STRUCTURE, UBUF_POOL, SHARED_POOL, SHARED) \
/** @internal @This allocates the data structure or fetches it from the     \
 * pool.                                                                    \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 * @return pointer to STRUCTURE or NULL in case of allocation error         \
 */                                                                         \
static struct STRUCTURE *STRUCTURE##_alloc_pool(struct ubuf_mgr *mgr)       \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    struct STRUCTURE *mem = upool_alloc(&mem_mgr->UBUF_POOL,                \
                                        struct STRUCTURE *);                \
    if (unlikely(mem == NULL))                                              \
        return NULL;                                                        \
    mem->SHARED = NULL;                                                     \
    return mem;                                                             \
}                                                                           \
/** @internal @This allocates the shared data structure or fetches it from  \
 * the pool.                                                                \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 * @return pointer to ubuf_mem_shared or NULL in case of allocation error   \
 */                                                                         \
static struct ubuf_mem_shared                                               \
    *STRUCTURE##_shared_alloc_pool(struct ubuf_mgr *mgr)                    \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    struct ubuf_mem_shared *shared = upool_alloc(&mem_mgr->SHARED_POOL,     \
                                                 struct ubuf_mem_shared *); \
    if (unlikely(shared == NULL))                                           \
        return NULL;                                                        \
    uatomic_store(&shared->refcount, 1);                                    \
    return shared;                                                          \
}                                                                           \
/** @internal @This deallocates a data structure or places it back into     \
 * the pool.                                                                \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 * @param mem pointer to STRUCTURE                                          \
 */                                                                         \
static void STRUCTURE##_free_pool(struct ubuf_mgr *mgr,                     \
                                  struct STRUCTURE *mem)                    \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    upool_free(&mem_mgr->UBUF_POOL, mem);                                   \
}                                                                           \
/** @internal @This deallocates a shared data structure or places it back   \
 * into the pool.                                                           \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 * @param shared pointer to ubuf_mem_shared                                 \
 */                                                                         \
static void STRUCTURE##_shared_free_pool(struct ubuf_mgr *mgr,              \
                                         struct ubuf_mem_shared *shared)    \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    upool_free(&mem_mgr->SHARED_POOL, shared);                              \
}                                                                           \
/** @internal @This instructs an existing manager to release all structures \
 * currently kept in pools. It is intended as a debug tool only.            \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 */                                                                         \
static void STRUCTURE##_mgr_vacuum_pool(struct ubuf_mgr *mgr)               \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    upool_vacuum(&mem_mgr->UBUF_POOL);                                      \
    upool_vacuum(&mem_mgr->SHARED_POOL);                                    \
}                                                                           \
/** @internal @This is called on deallocation of the manager.               \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 */                                                                         \
static void STRUCTURE##_mgr_clean_pool(struct ubuf_mgr *mgr)                \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    upool_clean(&mem_mgr->UBUF_POOL);                                       \
    upool_clean(&mem_mgr->SHARED_POOL);                                     \
}                                                                           \
/** @internal @This returns the required size of extra data space for pools.\
 *                                                                          \
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool     \
 * @param shared_pool_depth maximum number of shared structures in the pool \
 * @return the required size of extra data space pools                      \
 */                                                                         \
static size_t STRUCTURE##_mgr_sizeof_pool(uint16_t ubuf_pool_depth,         \
                                          uint16_t shared_pool_depth)       \
{                                                                           \
    return upool_sizeof(ubuf_pool_depth) + upool_sizeof(shared_pool_depth); \
}                                                                           \
/** @internal @This is called on allocation of the manager.                 \
 *                                                                          \
 * @param mgr pointer to a ubuf manager                                     \
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool     \
 * @param shared_pool_depth maximum number of shared structures in the pool \
 * @param extra mandatory extra space allocated by the caller, with the size\
 * returned by @ref STRUCTURE##_mgr_sizeof_pool                             \
 */                                                                         \
static void STRUCTURE##_mgr_init_pool(struct ubuf_mgr *mgr,                 \
        uint16_t ubuf_pool_depth, uint16_t shared_pool_depth, void *extra,  \
        upool_alloc_cb ubuf_alloc_cb, upool_free_cb ubuf_free_cb)           \
{                                                                           \
    struct STRUCTURE##_mgr *mem_mgr = STRUCTURE##_mgr_from_ubuf_mgr(mgr);   \
    upool_init(&mem_mgr->UBUF_POOL, ubuf_pool_depth, extra,                 \
               ubuf_alloc_cb, ubuf_free_cb);                                \
    upool_init(&mem_mgr->SHARED_POOL, shared_pool_depth,                    \
               extra + upool_sizeof(ubuf_pool_depth),                       \
               ubuf_mem_shared_alloc_inner, ubuf_mem_shared_free_inner);    \
}

#ifdef __cplusplus
}
#endif
#endif

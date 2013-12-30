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
 * @short Upipe ubuf manager for block formats with umem storage
 */

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/urefcount.h>
#include <upipe/ulifo.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_common.h>
#include <upipe/ubuf_block_mem.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** default minimum extra space before buffer when unspecified */
#define UBUF_DEFAULT_PREPEND        32
/** default minimum extra space after buffer when unspecified */
#define UBUF_DEFAULT_APPEND         32
/** default alignement of buffer when unspecified */
#define UBUF_DEFAULT_ALIGN          0

/** @This is the low-level shared structure with reference counting, pointing
 * to the actual data. */
struct ubuf_block_mem_shared {
    /** number of blocks pointing to the memory area */
    uatomic_uint32_t refcount;
    /** umem structure pointing to buffer */
    struct umem umem;
};

/** @This is a super-set of the @ref ubuf (and @ref ubuf_block)
 * structure with private fields pointing to shared data. */
struct ubuf_block_mem {
    /** pointer to shared structure */
    struct ubuf_block_mem_shared *shared;
    /** block block_mem structure */
    struct ubuf_block ubuf_block;
};

UBASE_FROM_TO(ubuf_block_mem, ubuf, ubuf, ubuf_block.ubuf)

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members. */
struct ubuf_block_mem_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** alignment */
    size_t align;
    /** alignment offset */
    int align_offset;

    /** ubuf pool */
    struct ulifo ubuf_pool;
    /** ubuf shared pool */
    struct ulifo shared_pool;
    /** umem allocator */
    struct umem_mgr *umem_mgr;

    /** common management structure */
    struct ubuf_mgr mgr;
};

UBASE_FROM_TO(ubuf_block_mem_mgr, ubuf_mgr, ubuf_mgr, mgr)
UBASE_FROM_TO(ubuf_block_mem_mgr, urefcount, urefcount, urefcount)

/** @hidden */
static void ubuf_block_mem_free_inner(struct ubuf *ubuf);
/** @hidden */
static void
    ubuf_block_mem_shared_free_inner(struct ubuf_block_mem_shared *shared);

/** @This increments the reference count of a shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_block_mem_use(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    uatomic_fetch_add(&block_mem->shared->refcount, 1);
}

/** @This checks whether there is only one reference to the shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline enum ubase_err ubuf_block_mem_single(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    return uatomic_load(&block_mem->shared->refcount) == 1 ?
           UBASE_ERR_NONE : UBASE_ERR_BUSY;
}

/** @This returns the shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline uint8_t *ubuf_block_mem_buffer(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    return umem_buffer(&block_mem->shared->umem);
}

/** @This returns the size of the shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline size_t ubuf_block_mem_size(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    return umem_size(&block_mem->shared->umem);
}

/** @internal @This allocates the data structure or fetches it from the pool.
 *
 * @param mgr common management structure
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_block_mem_alloc_inner(struct ubuf_mgr *mgr)
{
    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf *ubuf = ulifo_pop(&block_mem_mgr->ubuf_pool, struct ubuf *);
    struct ubuf_block_mem *block_mem;
    if (ubuf == NULL) {
        block_mem = malloc(sizeof(struct ubuf_block_mem));
        if (unlikely(block_mem == NULL))
            return NULL;
        ubuf = ubuf_block_mem_to_ubuf(block_mem);
        ubuf->mgr = mgr;
    } else
        block_mem = ubuf_block_mem_from_ubuf(ubuf);

    block_mem->shared = NULL;
    ubuf_block_common_init(ubuf, false);

    return ubuf;
}

/** @This allocates a ubuf, a shared structure and a umem buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_BLOCK (sentinel)
 * @param args optional arguments (1st = size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_block_mem_alloc(struct ubuf_mgr *mgr,
                                         enum ubuf_alloc_type alloc_type,
                                         va_list args)
{
    assert(alloc_type == UBUF_ALLOC_BLOCK);
    int size = va_arg(args, int);
    assert(size >= 0);

    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf *ubuf = ubuf_block_mem_alloc_inner(mgr);
    if (unlikely(ubuf == NULL))
        return NULL;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    block_mem->shared = ulifo_pop(&block_mem_mgr->shared_pool,
                                  struct ubuf_block_mem_shared *);
    if (block_mem->shared == NULL) {
        block_mem->shared = malloc(sizeof(struct ubuf_block_mem_shared));
        if (unlikely(block_mem->shared == NULL)) {
            if (unlikely(!ulifo_push(&block_mem_mgr->ubuf_pool, ubuf)))
                ubuf_block_mem_free_inner(ubuf);
            return NULL;
        }

        uatomic_init(&block_mem->shared->refcount, 1);
    } else
        uatomic_store(&block_mem->shared->refcount, 1);

    size_t buffer_size = size + block_mem_mgr->align;
    if (unlikely(!umem_alloc(block_mem_mgr->umem_mgr, &block_mem->shared->umem,
                             buffer_size))) {
        if (unlikely(!ulifo_push(&block_mem_mgr->shared_pool,
                                 block_mem->shared)))
            ubuf_block_mem_shared_free_inner(block_mem->shared);
        if (unlikely(!ulifo_push(&block_mem_mgr->ubuf_pool, ubuf)))
            ubuf_block_mem_free_inner(ubuf);
        return NULL;
    }

    size_t offset = block_mem_mgr->align;
    if (block_mem_mgr->align)
        offset -= ((uintptr_t)ubuf_block_mem_buffer(ubuf) + offset +
                   block_mem_mgr->align_offset) % block_mem_mgr->align;
    ubuf_block_common_set(ubuf, offset, size);
    ubuf_block_common_set_buffer(ubuf, ubuf_block_mem_buffer(ubuf));

    ubuf_mgr_use(mgr);
    return ubuf;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @return false in case of error
 */
static bool ubuf_block_mem_dup(struct ubuf *ubuf, struct ubuf **new_ubuf_p)
{
    assert(new_ubuf_p != NULL);
    struct ubuf *new_ubuf = ubuf_block_mem_alloc_inner(ubuf->mgr);
    if (unlikely(new_ubuf == NULL))
        return UBASE_ERR_ALLOC;

    if (unlikely(!ubuf_block_common_dup(ubuf, new_ubuf))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    struct ubuf_block_mem *new_block = ubuf_block_mem_from_ubuf(new_ubuf);
    new_block->shared = block_mem->shared;
    ubuf_block_mem_use(new_ubuf);
    ubuf_mgr_use(new_ubuf->mgr);
    return UBASE_ERR_NONE;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @param offset offset in the buffer
 * @param size final size of the buffer
 * @return false in case of error
 */
static bool ubuf_block_mem_splice(struct ubuf *ubuf, struct ubuf **new_ubuf_p,
                                  int offset, int size)
{
    assert(new_ubuf_p != NULL);
    struct ubuf *new_ubuf = ubuf_block_mem_alloc_inner(ubuf->mgr);
    if (unlikely(new_ubuf == NULL))
        return UBASE_ERR_ALLOC;

    if (unlikely(!ubuf_block_common_splice(ubuf, new_ubuf, offset, size))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    struct ubuf_block_mem *new_block = ubuf_block_mem_from_ubuf(new_ubuf);
    new_block->shared = block_mem->shared;
    ubuf_block_mem_use(new_ubuf);
    ubuf_mgr_use(new_ubuf->mgr);
    return UBASE_ERR_NONE;
}

/** @This handles control commands.
 *
 * @param ubuf pointer to ubuf
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err ubuf_block_mem_control(struct ubuf *ubuf,
                                             enum ubuf_command command,
                                             va_list args)
{
    switch (command) {
        case UBUF_DUP: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            return ubuf_block_mem_dup(ubuf, new_ubuf_p);
        }
        case UBUF_SINGLE:
            return ubuf_block_mem_single(ubuf);

        case UBUF_SPLICE_BLOCK: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            int offset = va_arg(args, int);
            int size = va_arg(args, int);
            return ubuf_block_mem_splice(ubuf, new_ubuf_p, offset, size);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees a ubuf and all associated data structures.
 *
 * @param ubuf pointer to a ubuf structure to free
 */
static void ubuf_block_mem_free_inner(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    free(block_mem);
}

/** @internal @This frees a shared buffer.
 *
 * @param shared pointer to shared structure to free
 */
static void
    ubuf_block_mem_shared_free_inner(struct ubuf_block_mem_shared *shared)
{
    free(shared);
}

/** @This recycles or frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure
 */
static void ubuf_block_mem_free(struct ubuf *ubuf)
{
    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);

    ubuf_block_common_clean(ubuf);

    if (unlikely(uatomic_fetch_sub(&block_mem->shared->refcount, 1) == 1)) {
        umem_free(&block_mem->shared->umem);
        if (unlikely(!ulifo_push(&block_mem_mgr->shared_pool,
                                 block_mem->shared)))
            ubuf_block_mem_shared_free_inner(block_mem->shared);
    }

    if (unlikely(!ulifo_push(&block_mem_mgr->ubuf_pool, ubuf)))
        ubuf_block_mem_free_inner(ubuf);

    ubuf_mgr_release(ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr));
}

/** @internal @This instructs an existing ubuf_block_mem manager to release
 * all structures currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a ubuf manager
 */
static void ubuf_block_mem_mgr_vacuum(struct ubuf_mgr *mgr)
{
    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf *ubuf;
    struct ubuf_block_mem_shared *shared;

    while ((ubuf = ulifo_pop(&block_mem_mgr->ubuf_pool,
                             struct ubuf *)) != NULL)
        ubuf_block_mem_free_inner(ubuf);
    while ((shared = ulifo_pop(&block_mem_mgr->shared_pool,
                               struct ubuf_block_mem_shared *)) != NULL)
        ubuf_block_mem_shared_free_inner(shared);
}

/** @This handles manager control commands.
 *
 * @param mgr pointer to ubuf manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err ubuf_block_mem_mgr_control(struct ubuf_mgr *mgr,
                                                 enum ubuf_mgr_command command,
                                                 va_list args)
{
    switch (command) {
        case UBUF_MGR_VACUUM: {
            ubuf_block_mem_mgr_vacuum(mgr);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a ubuf manager.
 *
 * @param urefcount pointer to urefcount
 */
static void ubuf_block_mem_mgr_free(struct urefcount *urefcount)
{
    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_urefcount(urefcount);
    ubuf_block_mem_mgr_vacuum(ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr));
    ulifo_clean(&block_mem_mgr->ubuf_pool);
    ulifo_clean(&block_mem_mgr->shared_pool);
    umem_mgr_release(block_mem_mgr->umem_mgr);

    urefcount_clean(urefcount);
    free(block_mem_mgr);
}

/** @This allocates a new instance of the ubuf manager for block formats
 * using umem.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param align default alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_offset offset of the aligned octet, in octets (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_block_mem_mgr_alloc(uint16_t ubuf_pool_depth,
                                          uint16_t shared_pool_depth,
                                          struct umem_mgr *umem_mgr,
                                          int align, int align_offset)
{
    assert(umem_mgr != NULL);

    struct ubuf_block_mem_mgr *block_mem_mgr =
        malloc(sizeof(struct ubuf_block_mem_mgr) +
               ulifo_sizeof(ubuf_pool_depth) +
               ulifo_sizeof(shared_pool_depth));
    if (unlikely(block_mem_mgr == NULL))
        return NULL;

    ulifo_init(&block_mem_mgr->ubuf_pool, ubuf_pool_depth,
               (void *)block_mem_mgr + sizeof(struct ubuf_block_mem_mgr));
    ulifo_init(&block_mem_mgr->shared_pool, shared_pool_depth,
               (void *)block_mem_mgr + sizeof(struct ubuf_block_mem_mgr) +
               ulifo_sizeof(ubuf_pool_depth));
    block_mem_mgr->umem_mgr = umem_mgr;
    umem_mgr_use(umem_mgr);

    block_mem_mgr->align = align > 0 ? align : UBUF_DEFAULT_ALIGN;
    block_mem_mgr->align_offset = align_offset;

    urefcount_init(ubuf_block_mem_mgr_to_urefcount(block_mem_mgr),
                   ubuf_block_mem_mgr_free);
    block_mem_mgr->mgr.refcount =
        ubuf_block_mem_mgr_to_urefcount(block_mem_mgr);
    block_mem_mgr->mgr.type = UBUF_ALLOC_BLOCK;
    block_mem_mgr->mgr.ubuf_alloc = ubuf_block_mem_alloc;
    block_mem_mgr->mgr.ubuf_control = ubuf_block_mem_control;
    block_mem_mgr->mgr.ubuf_free = ubuf_block_mem_free;
    block_mem_mgr->mgr.ubuf_mgr_control = ubuf_block_mem_mgr_control;

    return ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr);
}

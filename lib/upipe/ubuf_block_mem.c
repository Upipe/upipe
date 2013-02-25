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
    /** refcount management structure */
    urefcount refcount;
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

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members. */
struct ubuf_block_mem_mgr {
    /** extra space added before */
    size_t prepend;
    /** extra space added after */
    size_t append;
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

/** @hidden */
static void ubuf_block_mem_free_inner(struct ubuf *ubuf);
/** @hidden */
static void
    ubuf_block_mem_shared_free_inner(struct ubuf_block_mem_shared *shared);

/** @internal @This returns the high-level ubuf structure.
 *
 * @param block_mem pointer to the ubuf_block_mem structure
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *
    ubuf_block_mem_to_ubuf(struct ubuf_block_mem *block_mem)
{
    return ubuf_block_to_ubuf(&block_mem->ubuf_block);
}

/** @internal @This returns the private ubuf_block_mem structure.
 *
 * @param ubuf pointer to ubuf
 * @return pointer to the ubuf_block_mem structure
 */
static inline struct ubuf_block_mem *ubuf_block_mem_from_ubuf(struct ubuf *ubuf)
{
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    return container_of(block, struct ubuf_block_mem, ubuf_block);
}

/** @internal @This returns the high-level ubuf_mgr structure.
 *
 * @param block_mem_mgr pointer to the ubuf_block_mem_mgr structure
 * @return pointer to the ubuf_mgr structure
 */
static inline struct ubuf_mgr *
    ubuf_block_mem_mgr_to_ubuf_mgr(struct ubuf_block_mem_mgr *block_mem_mgr)
{
    return &block_mem_mgr->mgr;
}

/** @internal @This returns the private ubuf_block_mem_mgr structure.
 *
 * @param mgr description structure of the ubuf manager
 * @return pointer to the ubuf_block_mem_mgr structure
 */
static inline struct ubuf_block_mem_mgr *
    ubuf_block_mem_mgr_from_ubuf_mgr(struct ubuf_mgr *mgr)
{
    return container_of(mgr, struct ubuf_block_mem_mgr, mgr);
}

/** @This increments the reference count of a shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_block_mem_use(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    urefcount_use(&block_mem->shared->refcount);
}

/** @This checks whether there is only one reference to the shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline bool ubuf_block_mem_single(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    return urefcount_single(&block_mem->shared->refcount);
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

/** @This reallocated the shared buffer.
 *
 * @param ubuf pointer to ubuf
 * @param size new size of the buffer
 */
static inline bool ubuf_block_mem_realloc(struct ubuf *ubuf, size_t size)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    bool ret = umem_realloc(&block_mem->shared->umem, size);
    if (likely(ret))
        block_mem->ubuf_block.buffer = ubuf_block_mem_buffer(ubuf);
    return ret;
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

        urefcount_init(&block_mem->shared->refcount);
    } else
        urefcount_reset(&block_mem->shared->refcount);

    size_t buffer_size = size + block_mem_mgr->prepend + block_mem_mgr->append +
                         block_mem_mgr->align;
    if (unlikely(!umem_alloc(block_mem_mgr->umem_mgr, &block_mem->shared->umem,
                             buffer_size))) {
        if (unlikely(!ulifo_push(&block_mem_mgr->shared_pool,
                                 block_mem->shared)))
            ubuf_block_mem_shared_free_inner(block_mem->shared);
        if (unlikely(!ulifo_push(&block_mem_mgr->ubuf_pool, ubuf)))
            ubuf_block_mem_free_inner(ubuf);
        return NULL;
    }

    size_t offset = block_mem_mgr->prepend + block_mem_mgr->align;
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
        return false;

    if (unlikely(!ubuf_block_common_dup(ubuf, new_ubuf))) {
        ubuf_free(new_ubuf);
        return false;
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    struct ubuf_block_mem *new_block = ubuf_block_mem_from_ubuf(new_ubuf);
    new_block->shared = block_mem->shared;
    ubuf_block_mem_use(new_ubuf);
    ubuf_mgr_use(new_ubuf->mgr);
    return true;
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
        return false;

    if (unlikely(!ubuf_block_common_splice(ubuf, new_ubuf, offset, size))) {
        ubuf_free(new_ubuf);
        return false;
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    struct ubuf_block_mem *new_block = ubuf_block_mem_from_ubuf(new_ubuf);
    new_block->shared = block_mem->shared;
    ubuf_block_mem_use(new_ubuf);
    ubuf_mgr_use(new_ubuf->mgr);
    return true;
}

/** @internal @This extends a block_mem ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_size required size of the buffer
 * @return false in case of error, or if the ubuf is shared, or if the operation
 * is not possible
 */
static bool ubuf_block_mem_extend(struct ubuf *ubuf, int new_size)
{
    if (!ubuf_block_mem_single(ubuf))
        return false;
    if (unlikely(!ubuf_block_mem_realloc(ubuf, new_size)))
        return false;
    ubuf_block_common_set_buffer(ubuf, ubuf_block_mem_buffer(ubuf));
    return true;
}

/** @This handles control commands.
 *
 * @param ubuf pointer to ubuf
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool ubuf_block_mem_control(struct ubuf *ubuf,
                                   enum ubuf_command command, va_list args)
{
    switch (command) {
        case UBUF_DUP: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            return ubuf_block_mem_dup(ubuf, new_ubuf_p);
        }
        case UBUF_SINGLE:
            return ubuf_block_mem_single(ubuf);

        case UBUF_EXTEND_BLOCK: {
            int new_size = va_arg(args, int);
            return ubuf_block_mem_extend(ubuf, new_size);
        }
        case UBUF_SPLICE_BLOCK: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            int offset = va_arg(args, int);
            int size = va_arg(args, int);
            return ubuf_block_mem_splice(ubuf, new_ubuf_p, offset, size);
        }
        default:
            return false;
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

    if (unlikely(urefcount_release(&block_mem->shared->refcount))) {
        umem_free(&block_mem->shared->umem);
        if (unlikely(!ulifo_push(&block_mem_mgr->shared_pool,
                                 block_mem->shared)))
            ubuf_block_mem_shared_free_inner(block_mem->shared);
    }

    if (unlikely(!ulifo_push(&block_mem_mgr->ubuf_pool, ubuf)))
        ubuf_block_mem_free_inner(ubuf);

    ubuf_mgr_release(ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr));
}

/** @This instructs an existing ubuf block_mem mem manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
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

/** @This frees a ubuf manager.
 *
 * @param mgr pointer to a ubuf manager
 */
static void ubuf_block_mem_mgr_free(struct ubuf_mgr *mgr)
{
    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(mgr);
    ubuf_block_mem_mgr_vacuum(mgr);
    ulifo_clean(&block_mem_mgr->ubuf_pool);
    ulifo_clean(&block_mem_mgr->shared_pool);
    umem_mgr_release(block_mem_mgr->umem_mgr);

    urefcount_clean(&block_mem_mgr->mgr.refcount);
    free(block_mem_mgr);
}

/** @This allocates a new instance of the ubuf manager for block formats
 * using umem.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param prepend default minimum extra space before buffer (if set to -1, a
 * default sensible value is used)
 * @param append default minimum extra space after buffer (if set to -1, a
 * default sensible value is used)
 * @param align default alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_offset offset of the aligned octet, in octets (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_block_mem_mgr_alloc(uint16_t ubuf_pool_depth,
                                          uint16_t shared_pool_depth,
                                          struct umem_mgr *umem_mgr,
                                          int prepend, int append,
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

    block_mem_mgr->prepend = prepend >= 0 ? prepend : UBUF_DEFAULT_PREPEND;
    block_mem_mgr->append = append >= 0 ? append : UBUF_DEFAULT_APPEND;
    block_mem_mgr->align = align > 0 ? align : UBUF_DEFAULT_ALIGN;
    block_mem_mgr->align_offset = align_offset;

    urefcount_init(&block_mem_mgr->mgr.refcount);
    block_mem_mgr->mgr.type = UBUF_ALLOC_BLOCK;
    block_mem_mgr->mgr.ubuf_alloc = ubuf_block_mem_alloc;
    block_mem_mgr->mgr.ubuf_control = ubuf_block_mem_control;
    block_mem_mgr->mgr.ubuf_free = ubuf_block_mem_free;
    block_mem_mgr->mgr.ubuf_mgr_vacuum = ubuf_block_mem_mgr_vacuum;
    block_mem_mgr->mgr.ubuf_mgr_free = ubuf_block_mem_mgr_free;

    return ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr);
}

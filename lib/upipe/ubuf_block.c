/*****************************************************************************
 * ubuf_block.c: struct ubuf manager for block formats
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

/*
 * Please note that you must maintain at least one manager per thread,
 * because due to the pool implementation, only one thread can make
 * allocations (structures can be released from any thread though).
 */

/*
 * NB: All block managers are compatible in the manner requested by
 * ubuf_writable() and ubuf_block_resize() for the new manager, ie. they
 * store data "in the same manner", even if the prepend, append and align
 * options are different.
 */

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/upool.h>
#include <upipe/ubuf_block.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE           4096
/** default minimum extra space before buffer when unspecified */
#define UBUF_DEFAULT_PREPEND        32
/** default minimum extra space after buffer when unspecified */
#define UBUF_DEFAULT_APPEND         32
/** default alignement of buffer when unspecified */
#define UBUF_DEFAULT_ALIGN          16

/** super-set of the ubuf_mgr structure with additional local members */
struct ubuf_block_mgr {
    /** default size */
    size_t size;
    /** extra space added before */
    size_t prepend;
    /** extra space added after */
    size_t append;
    /** alignment */
    size_t align;
    /** alignment offset */
    int align_offset;

    /** struct ubuf pool for packets <= size */
    struct upool small_pool;

    /** struct ubuf pool for packets > size */
    struct upool big_pool;

    /** common management structure */
    struct ubuf_mgr mgr;
};

/** super-set of the ubuf structure with additional local members */
struct ubuf_block {
    /** extra space allocated at the end of the structure */
    size_t extra_space;
    /** currently exported size of the buffer */
    size_t size;

    /** common structure */
    struct ubuf ubuf;
};

/** @internal @This returns the high-level ubuf structure.
 *
 * @param block pointer to the ubuf_block structure
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *ubuf_block_to_ubuf(struct ubuf_block *block)
{
    return &block->ubuf;
}

/** @internal @This returns the private ubuf_block structure.
 *
 * @param mgr description structure of the ubuf mgr
 * @return pointer to the ubuf_block structure
 */
static inline struct ubuf_block *ubuf_block_from_ubuf(struct ubuf *ubuf)
{
    return container_of(ubuf, struct ubuf_block, ubuf);
}

/** @internal @This returns the high-level ubuf_mgr structure.
 *
 * @param block_mgr pointer to the ubuf_block_mgr structure
 * @return pointer to the ubuf_mgr structure
 */
static inline struct ubuf_mgr *ubuf_block_mgr_to_ubuf_mgr(struct ubuf_block_mgr *block_mgr)
{
    return &block_mgr->mgr;
}

/** @internal @This returns the private ubuf_block_mgr structure.
 *
 * @param mgr description structure of the ubuf mgr
 * @return pointer to the ubuf_block_mgr structure
 */
static inline struct ubuf_block_mgr *ubuf_block_mgr_from_ubuf_mgr(struct ubuf_mgr *mgr)
{
    return container_of(mgr, struct ubuf_block_mgr, mgr);
}

/** @internal @This allocates the data structure or fetches it from the pool.
 *
 * @param mgr common management structure
 * @param size requested size of the buffer
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_block_alloc_inner(struct ubuf_mgr *mgr, size_t size)
{
    struct ubuf_block_mgr *block_mgr = ubuf_block_mgr_from_ubuf_mgr(mgr);
    size_t extra_space = size + block_mgr->prepend + block_mgr->append +
                         block_mgr->align;
    struct ubuf_block *block = NULL;
    struct uchain *uchain;
    if (likely(size <= block_mgr->size))
        uchain = upool_pop(&block_mgr->small_pool);
    else
        uchain = upool_pop(&block_mgr->big_pool);
    if (likely(uchain != NULL))
        block = ubuf_block_from_ubuf(ubuf_from_uchain(uchain));

    if (unlikely(block == NULL)) {
        block = malloc(sizeof(struct ubuf_block) + sizeof(struct ubuf_plane) +
                       extra_space);
        if (unlikely(block == NULL)) return NULL;
        block->extra_space = extra_space;
        block->ubuf.mgr = mgr;
    } else if (unlikely(block->extra_space < extra_space)) {
        struct ubuf_block *old_block = block;
        block = realloc(block, sizeof(struct ubuf_block) +
                               sizeof(struct ubuf_plane) + extra_space);
        if (unlikely(block == NULL)) {
            free(old_block);
            return NULL;
        }
        block->extra_space = extra_space;
    }

    block->size = size;
    struct ubuf *ubuf = ubuf_block_to_ubuf(block);
    ubuf->planes[0].stride = 0;
    ubuf->planes[0].buffer = (uint8_t *)block + sizeof(struct ubuf_block) +
                             sizeof(struct ubuf_plane) + block_mgr->prepend +
                             block_mgr->align;
    ubuf->planes[0].buffer -= ((uintptr_t)ubuf->planes[0].buffer +
                               block_mgr->align_offset) % block_mgr->align;
    urefcount_init(&ubuf->refcount);
    ubuf_mgr_use(mgr);

    uchain_init(&ubuf->uchain);
    return ubuf;
}

/** @This allocates a ubuf and a block buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_TYPE_BLOCK (sentinel)
 * @param args optional arguments (1st = size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *_ubuf_block_alloc(struct ubuf_mgr *mgr,
                                      enum ubuf_alloc_type alloc_type,
                                      va_list args)
{
    assert(alloc_type == UBUF_ALLOC_TYPE_BLOCK);
    struct ubuf_block_mgr *block_mgr = ubuf_block_mgr_from_ubuf_mgr(mgr);
    size_t size = block_mgr->size;
    int arg;

    /* Parse arguments */
    arg = va_arg(args, int); if (arg >= 0) size = arg;

    return ubuf_block_alloc_inner(mgr, size);
}

/** @This duplicates a ubuf and its block buffer.
 *
 * @param mgr management structure used to create the new block buffer
 * @param ubuf ubuf structure to duplicate
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *_ubuf_block_dup(struct ubuf_mgr *mgr, struct ubuf *ubuf)
{
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    struct ubuf *new_ubuf = ubuf_block_alloc_inner(mgr, block->size);
    if (unlikely(new_ubuf == NULL)) return NULL;

    memcpy(new_ubuf->planes[0].buffer, ubuf->planes[0].buffer, block->size);

    return new_ubuf;
}

/** @internal @This frees a ubuf and all associated data structures.
 *
 * @param block pointer to a ubuf_block structure to free
 */
static void _ubuf_block_free_inner(struct ubuf_block *block)
{
    urefcount_clean(&block->ubuf.refcount);
    free(block);
}

/** @This frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure to free
 */
static void _ubuf_block_free(struct ubuf *ubuf)
{
    struct ubuf_block_mgr *block_mgr = ubuf_block_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);

    if (likely(block->extra_space <= block_mgr->size + block_mgr->prepend +
                                     block_mgr->append + block_mgr->align)) {
        if (likely(upool_push(&block_mgr->small_pool, &ubuf->uchain)))
            block = NULL;
    } else {
        if (likely(upool_push(&block_mgr->big_pool, &ubuf->uchain)))
            block = NULL;
    }
    if (unlikely(block != NULL))
        _ubuf_block_free_inner(block);

    ubuf_mgr_release(&block_mgr->mgr);
}

/** @This resizes or re-allocates a ubuf with less or more space.
 *
 * @param mgr management structure used to create a new buffer, if needed
 * (can be NULL if ubuf_single(ubuf))
 * @param ubuf_p reference to a pointer to a ubuf to resize
 * @param alloc_type must be UBUF_ALLOC_TYPE_BLOCK (sentinel)
 * @param args optional arguments (1st = size after operation,
 * 2nd = number of octets to skip at the beginning, can be negative)
 * @return false in case of allocation error (unchanged uref)
 */
static bool _ubuf_block_resize(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                               enum ubuf_alloc_type alloc_type, va_list args)
{
    assert(alloc_type == UBUF_ALLOC_TYPE_BLOCK);
    struct ubuf *ubuf = *ubuf_p;
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    int new_size = -1;
    int skip;
    int arg;

    /* parse arguments */
    arg = va_arg(args, int); if (arg >= 0) new_size = arg;
    arg = va_arg(args, int); skip = arg;
    if (unlikely(new_size == -1))
        new_size = block->size - skip;

    if (unlikely(skip < 0 && new_size < -skip)) return false;
    if (unlikely(skip >= 0 && block->size < skip)) return false;

    /* if ubuf is in use, allocate a new one with the needed size */
    if (unlikely(!ubuf_single(ubuf))) {
        assert(mgr != NULL);
        struct ubuf *new_ubuf = ubuf_block_alloc(mgr, new_size);
        if (unlikely(new_ubuf == NULL)) return false;

        if (likely(skip >= 0))
            memcpy(new_ubuf->planes[0].buffer,
                   ubuf->planes[0].buffer + skip,
                   likely(new_size <= block->size - skip) ?
                       new_size : block->size - skip);
        else
            memcpy(new_ubuf->planes[0].buffer - skip,
                   ubuf->planes[0].buffer,
                   likely(new_size + skip <= block->size) ?
                       new_size + skip : block->size);

        ubuf_release(ubuf);
        *ubuf_p = new_ubuf;
        return true;
    }

    ptrdiff_t offset = ubuf->planes[0].buffer - (uint8_t *)block;
    ptrdiff_t lower = sizeof(struct ubuf_block) + sizeof(struct ubuf_plane);
    ptrdiff_t higher = lower + block->extra_space;

    /* try just changing the pointers */
    if (likely(offset + skip >= lower &&
               offset + skip + new_size <= higher)) {
        ubuf->planes[0].buffer += skip;
        block->size = new_size;
        return true;
    }

    /* try just extending the buffer with realloc() */
    if (likely(offset + skip >= lower)) {
        int append = new_size - (higher - offset);
        if (unlikely(skip >= 0)) append += skip;
        block = realloc(block, sizeof(struct ubuf_block) +
                               sizeof(struct ubuf_plane) +
                               block->extra_space + append);
        if (unlikely(block == NULL)) return false;
        block->extra_space += append;
        ubuf = ubuf_block_to_ubuf(block);
        ubuf->planes[0].buffer = (uint8_t *)block + offset + skip;
        block->size = new_size;
        *ubuf_p = ubuf;
        return true;
    }

    /* check if moving the data into the buffer is enough */
    if (unlikely(block->extra_space < new_size)) {
        block = realloc(block, sizeof(struct ubuf_block) +
                               sizeof(struct ubuf_plane) + new_size);
        if (unlikely(block == NULL)) return false;
        block->extra_space = new_size;
        ubuf = ubuf_block_to_ubuf(block);
        *ubuf_p = ubuf;
    }

    ubuf->planes[0].buffer = (uint8_t *)block + sizeof(struct ubuf_block) +
                             sizeof(struct ubuf_plane);
    if (likely(skip < 0))
        memmove(ubuf->planes[0].buffer - skip,
                (uint8_t *)block + offset,
                likely(new_size + skip <= block->size) ?
                    new_size + skip : block->size);
    else
        memmove(ubuf->planes[0].buffer,
                (uint8_t *)block + offset + skip,
                likely(new_size <= block->size - skip) ?
                    new_size : block->size - skip);
    block->size = new_size;
    return true;
}

/** @This frees a ubuf_mgr structure.
 *
 * @param mgr pointer to a ubuf_mgr structure to free
 */
static void _ubuf_block_mgr_free(struct ubuf_mgr *mgr)
{
    struct ubuf_block_mgr *block_mgr = ubuf_block_mgr_from_ubuf_mgr(mgr);
    struct uchain *uchain;

    while ((uchain = upool_pop(&block_mgr->small_pool)) != NULL) {
        struct ubuf_block *block = ubuf_block_from_ubuf(ubuf_from_uchain(uchain));
        _ubuf_block_free_inner(block);
    }
    upool_clean(&block_mgr->small_pool);

    while ((uchain = upool_pop(&block_mgr->big_pool)) != NULL) {
        struct ubuf_block *block = ubuf_block_from_ubuf(ubuf_from_uchain(uchain));
        _ubuf_block_free_inner(block);
    }
    upool_clean(&block_mgr->big_pool);

    urefcount_clean(&block_mgr->mgr.refcount);
    free(block_mgr);
}

/** @This allocates a new instance of the ubuf manager for block formats.
 *
 * @param small_pool_depth maximum number of small blocks in the pool
 * @param big_pool_depth maximum number of big blocks in the pool
 * @param size limit between small blocks (including) and big
 * blocks (excluding); also default block size when none indicated (if set to
 * -1, a default sensible value is used)
 * @param prepend default minimum extra space before buffer (if set to -1, a
 * default sensible value is used)
 * @param append default minimum extra space after buffer (if set to -1, a
 * default sensible value is used)
 * @param align default alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_offset offset of the aligned octet, in octets (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_block_mgr_alloc(unsigned int small_pool_depth,
                                      unsigned int big_pool_depth, int size,
                                      int prepend, int append, int align,
                                      int align_offset)
{
    struct ubuf_block_mgr *block_mgr = malloc(sizeof(struct ubuf_block_mgr));
    if (unlikely(block_mgr == NULL)) return NULL;

    upool_init(&block_mgr->small_pool, small_pool_depth);
    upool_init(&block_mgr->big_pool, big_pool_depth);
    block_mgr->size = size >= 0 ? size : UBUF_DEFAULT_SIZE;
    block_mgr->prepend = prepend >= 0 ? prepend : UBUF_DEFAULT_PREPEND;
    block_mgr->append = append >= 0 ? append : UBUF_DEFAULT_APPEND;
    block_mgr->align = align > 0 ? align : UBUF_DEFAULT_ALIGN;
    block_mgr->align_offset = align_offset;

    urefcount_init(&block_mgr->mgr.refcount);
    block_mgr->mgr.ubuf_alloc = _ubuf_block_alloc;
    block_mgr->mgr.ubuf_dup = _ubuf_block_dup;
    block_mgr->mgr.ubuf_free = _ubuf_block_free;
    block_mgr->mgr.ubuf_resize = _ubuf_block_resize;
    block_mgr->mgr.ubuf_mgr_free = _ubuf_block_mgr_free;

    return ubuf_block_mgr_to_ubuf_mgr(block_mgr);
}

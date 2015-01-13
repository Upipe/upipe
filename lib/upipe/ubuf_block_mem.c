/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
#include <upipe/upool.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_common.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_mem_common.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** default alignement of buffer when unspecified */
#define UBUF_DEFAULT_ALIGN          0

/** @This is a super-set of the @ref ubuf (and @ref ubuf_block)
 * structure with private fields pointing to shared data. */
struct ubuf_block_mem {
    /** pointer to shared structure */
    struct ubuf_mem_shared *shared;

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
    struct upool ubuf_pool;
    /** ubuf shared pool */
    struct upool shared_pool;
    /** umem allocator */
    struct umem_mgr *umem_mgr;

    /** common management structure */
    struct ubuf_mgr mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(ubuf_block_mem_mgr, ubuf_mgr, ubuf_mgr, mgr)
UBASE_FROM_TO(ubuf_block_mem_mgr, urefcount, urefcount, urefcount)
UBASE_FROM_TO(ubuf_block_mem_mgr, upool, ubuf_pool, ubuf_pool)

UBUF_MEM_MGR_HELPER_POOL(ubuf_block_mem, ubuf_pool, shared_pool, shared)

/** @This allocates a ubuf, a shared structure and a umem buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_BLOCK (sentinel)
 * @param args optional arguments (1st = size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_block_mem_alloc(struct ubuf_mgr *mgr,
                                         uint32_t signature, va_list args)
{
    if (unlikely(signature != UBUF_ALLOC_BLOCK))
        return NULL;

    int size = va_arg(args, int);
    assert(size >= 0);

    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf_block_mem *block_mem = ubuf_block_mem_alloc_pool(mgr);
    if (unlikely(block_mem == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_block_mem_to_ubuf(block_mem);
    ubuf_block_common_init(ubuf, false);

    block_mem->shared = ubuf_block_mem_shared_alloc_pool(mgr);
    if (unlikely(block_mem->shared == NULL)) {
        ubuf_block_mem_free_pool(mgr, block_mem);
        return NULL;
    }

    size_t buffer_size = size + block_mem_mgr->align;
    if (unlikely(!umem_alloc(block_mem_mgr->umem_mgr, &block_mem->shared->umem,
                             buffer_size))) {
        ubuf_block_mem_shared_free_pool(mgr, block_mem->shared);
        ubuf_block_mem_free_pool(mgr, block_mem);
        return NULL;
    }

    size_t offset = block_mem_mgr->align;
    if (block_mem_mgr->align)
        offset -= ((uintptr_t)ubuf_mem_shared_buffer(block_mem->shared) +
                  offset + block_mem_mgr->align_offset) % block_mem_mgr->align;
    ubuf_block_common_set(ubuf, offset, size);
    ubuf_block_common_set_buffer(ubuf,
                                 ubuf_mem_shared_buffer(block_mem->shared));

    ubuf_mgr_use(mgr);
    return ubuf;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @return an error code
 */
static int ubuf_block_mem_dup(struct ubuf *ubuf, struct ubuf **new_ubuf_p)
{
    assert(new_ubuf_p != NULL);
    struct ubuf_block_mem *new_block = ubuf_block_mem_alloc_pool(ubuf->mgr);
    if (unlikely(new_block == NULL))
        return UBASE_ERR_ALLOC;

    struct ubuf *new_ubuf = ubuf_block_mem_to_ubuf(new_block);
    ubuf_block_common_init(new_ubuf, false);
    if (unlikely(!ubase_check(ubuf_block_common_dup(ubuf, new_ubuf)))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    new_block->shared = ubuf_mem_shared_use(block_mem->shared);
    ubuf_mgr_use(new_ubuf->mgr);
    return UBASE_ERR_NONE;
}

/** @This checks whether there is only one reference to the shared buffer.
 *
 * @param ubuf pointer to ubuf
 * @return an error code
 */
static int ubuf_block_mem_single(struct ubuf *ubuf)
{
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    return ubuf_mem_shared_single(block_mem->shared) ?
           UBASE_ERR_NONE : UBASE_ERR_BUSY;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @param offset offset in the buffer
 * @param size final size of the buffer
 * @return an error code
 */
static int ubuf_block_mem_splice(struct ubuf *ubuf, struct ubuf **new_ubuf_p,
                                 int offset, int size)
{
    assert(new_ubuf_p != NULL);
    struct ubuf_block_mem *new_block = ubuf_block_mem_alloc_pool(ubuf->mgr);
    if (unlikely(new_block == NULL))
        return UBASE_ERR_ALLOC;

    struct ubuf *new_ubuf = ubuf_block_mem_to_ubuf(new_block);
    ubuf_block_common_init(new_ubuf, false);
    if (unlikely(!ubase_check(ubuf_block_common_splice(ubuf, new_ubuf,
                                                       offset, size)))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);
    new_block->shared = ubuf_mem_shared_use(block_mem->shared);
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
static int ubuf_block_mem_control(struct ubuf *ubuf, int command, va_list args)
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

/** @This recycles or frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure
 */
static void ubuf_block_mem_free(struct ubuf *ubuf)
{
    struct ubuf_mgr *mgr = ubuf->mgr;
    struct ubuf_block_mem *block_mem = ubuf_block_mem_from_ubuf(ubuf);

    ubuf_block_common_clean(ubuf);

    if (unlikely(ubuf_mem_shared_release(block_mem->shared))) {
        umem_free(&block_mem->shared->umem);
        ubuf_block_mem_shared_free_pool(mgr, block_mem->shared);
    }
    ubuf_block_mem_free_pool(mgr, block_mem);
    ubuf_mgr_release(mgr);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to ubuf_block_mem or NULL in case of allocation error
 */
static void *ubuf_block_mem_alloc_inner(struct upool *upool)
{
    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_pool(upool);
    struct ubuf_block_mem *block_mem = malloc(sizeof(struct ubuf_block_mem));
    struct ubuf_mgr *mgr = ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr);
    if (unlikely(block_mem == NULL))
        return NULL;
    struct ubuf *ubuf = ubuf_block_mem_to_ubuf(block_mem);
    ubuf->mgr = mgr;
    return block_mem;
}

/** @internal @This frees a ubuf_block_mem.
 *
 * @param upool pointer to upool
 * @param _block_mem pointer to a ubuf_block_mem structure to free
 */
static void ubuf_block_mem_free_inner(struct upool *upool, void *_block_mem)
{
    struct ubuf_block_mem *block_mem = (struct ubuf_block_mem *)_block_mem;
    free(block_mem);
}

/** @This checks if the given flow format can be allocated with the manager.
 *
 * @param mgr pointer to ubuf manager
 * @param flow_format flow format to check
 * @return an error code
 */
static int ubuf_block_mem_mgr_check(struct ubuf_mgr *mgr,
                                    struct uref *flow_format)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_format, &def))
    if (ubase_ncmp(def, "block."))
        return UBASE_ERR_INVALID;

    uint64_t align = 0;
    int64_t align_offset = 0;
    uref_block_flow_get_align(flow_format, &align);
    uref_block_flow_get_align_offset(flow_format, &align_offset);

    struct ubuf_block_mem_mgr *block_mem_mgr =
        ubuf_block_mem_mgr_from_ubuf_mgr(mgr);
    if (align && (block_mem_mgr->align % align ||
                  block_mem_mgr->align_offset != align_offset))
        return UBASE_ERR_INVALID;
    return UBASE_ERR_NONE;
}

/** @This handles manager control commands.
 *
 * @param mgr pointer to ubuf manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_block_mem_mgr_control(struct ubuf_mgr *mgr,
                                      int command, va_list args)
{
    switch (command) {
        case UBUF_MGR_CHECK: {
            struct uref *flow_format = va_arg(args, struct uref *);
            return ubuf_block_mem_mgr_check(mgr, flow_format);
        }
        case UBUF_MGR_VACUUM: {
            ubuf_block_mem_mgr_vacuum_pool(mgr);
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
    struct ubuf_mgr *mgr = ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr);
    ubuf_block_mem_mgr_clean_pool(mgr);
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
               ubuf_block_mem_mgr_sizeof_pool(ubuf_pool_depth,
                                              shared_pool_depth));
    if (unlikely(block_mem_mgr == NULL))
        return NULL;

    ubuf_block_mem_mgr_init_pool(ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr),
            ubuf_pool_depth, shared_pool_depth, block_mem_mgr->upool_extra,
            ubuf_block_mem_alloc_inner, ubuf_block_mem_free_inner);

    block_mem_mgr->umem_mgr = umem_mgr;
    umem_mgr_use(umem_mgr);

    block_mem_mgr->align = align > 0 ? align : UBUF_DEFAULT_ALIGN;
    block_mem_mgr->align_offset = align_offset;

    urefcount_init(ubuf_block_mem_mgr_to_urefcount(block_mem_mgr),
                   ubuf_block_mem_mgr_free);
    block_mem_mgr->mgr.refcount =
        ubuf_block_mem_mgr_to_urefcount(block_mem_mgr);
    block_mem_mgr->mgr.signature = UBUF_ALLOC_BLOCK;
    block_mem_mgr->mgr.ubuf_alloc = ubuf_block_mem_alloc;
    block_mem_mgr->mgr.ubuf_control = ubuf_block_mem_control;
    block_mem_mgr->mgr.ubuf_free = ubuf_block_mem_free;
    block_mem_mgr->mgr.ubuf_mgr_control = ubuf_block_mem_mgr_control;

    return ubuf_block_mem_mgr_to_ubuf_mgr(block_mem_mgr);
}

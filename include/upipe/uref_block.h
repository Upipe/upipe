/*****************************************************************************
 * uref_block.h: block semantics for uref and ubuf structures
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

#ifndef _UPIPE_UREF_BLOCK_H_
/** @hidden */
#define _UPIPE_UREF_BLOCK_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>

#include <stdint.h>
#include <assert.h>

UREF_ATTR_TEMPLATE(block, offset, "b.offset", unsigned, uint64_t, block offset)
UREF_ATTR_TEMPLATE(block, size, "b.size", unsigned, uint64_t, block size)

/** @This returns a new uref pointing to a new ubuf pointing to a block.
 * This is equivalent to the two operations sequentially, and is a shortcut.
 *
 * @param uref_mgr management structure for this uref pool
 * @param ubuf_mgr management structure for this ubuf pool
 * @param size size of the buffer
 * @return pointer to uref or NULL in case of failure
 */
static inline struct uref *uref_block_alloc(struct uref_mgr *uref_mgr,
                                            struct ubuf_mgr *ubuf_mgr,
                                            size_t size)
{
    struct uref *uref = uref_ubuf_alloc(uref_mgr, ubuf_mgr,
                                        UBUF_ALLOC_TYPE_BLOCK, size);
    if (unlikely(uref == NULL)) return NULL;

    if (unlikely(!uref_block_set_size(&uref, size))) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

/** @This returns a pointer to the buffer space.
 *
 * @param uref struct uref structure
 * @param size_p reference written with the size of the buffer space
 * @return pointer to buffer space or NULL in case of error
 */
static inline uint8_t *uref_block_buffer(struct uref *uref, size_t *size_p)
{
    if (unlikely(uref->ubuf == NULL)) {
        *size_p = 0;
        return NULL;
    }

    uint64_t offset = 0;
    uref_block_get_offset(uref, &offset);
    if (likely(size_p != NULL)) {
        uint64_t size = 0;
        uref_block_get_size(uref, &size);
        *size_p = size;
    }
    return uref->ubuf->planes[0].buffer + offset;
}

/** @This resizes the buffer space pointed to by a uref, in an efficient
 * manner.
 *
 * @param uref_p reference to a uref structure
 * @param ubuf_mgr ubuf management structure in case duplication is needed
 * (may be NULL if the resize is only a space reduction)
 * @param new_size final size of the buffer (if set to -1, keep same buffer
 * end)
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @return true if the operation succeeded
 */
static inline bool uref_block_resize(struct uref **uref_p,
                                     struct ubuf_mgr *ubuf_mgr,
                                     int new_size, int skip)
{
    struct uref *uref = *uref_p;
    assert(uref->ubuf != NULL);
    bool ret;
    uint64_t offset = 0;
    uint64_t size = 0, max_size;
    uref_block_get_offset(uref, &offset);
    uref_block_get_size(uref, &size);
    max_size = size + offset;
    if (unlikely(new_size == -1))
        new_size = size - skip;

    if (unlikely(skip < 0 && new_size < -skip)) return false;
    if (unlikely(skip >= 0 && size < skip)) return false;

    /* if the buffer is not shared, the manager implementation is faster */
    if (likely(ubuf_single(uref->ubuf)))
        goto uref_block_resize_ubuf;

    /* try just changing the attributes */
    if (likely((int64_t)offset + skip >= 0 &&
               (int64_t)offset + skip + new_size <= max_size)) {
        if (likely(skip != 0))
            if (unlikely(!uref_block_set_offset(uref_p, offset + skip)))
                return false;
        if (likely(new_size != size))
            if (unlikely(!uref_block_set_size(uref_p, new_size)))
                return false;
        return true;
    }

    /* we'll have to change the ubuf */
    assert(ubuf_mgr != NULL);

uref_block_resize_ubuf:
    ret = ubuf_block_resize(ubuf_mgr, &uref->ubuf, new_size, offset + skip);
    if (likely(ret)) {
        if (unlikely(!uref_block_set_size(uref_p, new_size)))
            return false;
        uref_block_delete_offset(*uref_p);
    }
    return ret;
}

#define uref_block_release uref_release
#define uref_block_writable uref_ubuf_writable
#define uref_block_dup uref_dup

#endif

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

/** @file
 * @short Upipe wrapper for block ubuf and uref
 */

#ifndef _UPIPE_UREF_BLOCK_H_
/** @hidden */
#define _UPIPE_UREF_BLOCK_H_

#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>
#include <stdbool.h>
#include <sys/uio.h>

UREF_ATTR_TEMPLATE_VOID(block, discontinuity, "b.discontinuity", discontinuity)
UREF_ATTR_TEMPLATE_VOID(block, error, "b.error", transport error)
UREF_ATTR_TEMPLATE_VOID(block, start, "b.start", start)
UREF_ATTR_TEMPLATE_VOID(block, end, "b.end", end)

/** @This returns a new uref pointing to a new ubuf pointing to a block.
 * This is equivalent to the two operations sequentially, and is a shortcut.
 *
 * @param uref_mgr management structure for this uref type
 * @param ubuf_mgr management structure for this ubuf type
 * @param size size of the buffer
 * @return pointer to uref or NULL in case of failure
 */
static inline struct uref *uref_block_alloc(struct uref_mgr *uref_mgr,
                                            struct ubuf_mgr *ubuf_mgr,
                                            int size)
{
    struct uref *uref = uref_alloc(uref_mgr);
    if (unlikely(uref == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_block_alloc(ubuf_mgr, size);
    if (unlikely(ubuf == NULL)) {
        uref_free(uref);
        return NULL;
    }

    uref_attach_ubuf(uref, ubuf);
    return uref;
}

/** @see ubuf_block_size */
static inline bool uref_block_size(struct uref *uref, size_t *size_p)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_size(uref->ubuf, size_p);
}

/** @see ubuf_block_read */
static inline bool uref_block_read(struct uref *uref, int offset, int *size_p,
                                   const uint8_t **buffer_p)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_read(uref->ubuf, offset, size_p, buffer_p);
}

/** @see ubuf_block_write */
static inline bool uref_block_write(struct uref *uref, int offset, int *size_p,
                                    uint8_t **buffer_p)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_write(uref->ubuf, offset, size_p, buffer_p);
}

/** @see ubuf_block_unmap */
static inline bool uref_block_unmap(struct uref *uref, int offset, int size)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_unmap(uref->ubuf, offset, size);
}

/** @see ubuf_block_insert */
static inline bool uref_block_insert(struct uref *uref, int offset,
                                     struct ubuf *insert)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_insert(uref->ubuf, offset, insert);
}

/** @see ubuf_block_append */
static inline bool uref_block_append(struct uref *uref, struct ubuf *append)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_append(uref->ubuf, append);
}

/** @see ubuf_block_delete */
static inline bool uref_block_delete(struct uref *uref, int offset, int size)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_delete(uref->ubuf, offset, size);
}

/** @see ubuf_block_peek */
static inline const uint8_t *uref_block_peek(struct uref *uref,
                                             int offset, int size,
                                             uint8_t *buffer)
{
    if (uref->ubuf == NULL)
        return NULL;
    return ubuf_block_peek(uref->ubuf, offset, size, buffer);
}

/** @see ubuf_block_peek_unmap */
static inline bool uref_block_peek_unmap(struct uref *uref,
                                         int offset, int size, uint8_t *buffer,
                                         const uint8_t *read_buffer)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_peek_unmap(uref->ubuf, offset, size, buffer, read_buffer);
}

/** @see ubuf_block_extract */
static inline bool uref_block_extract(struct uref *uref, int offset, int size,
                                      uint8_t *buffer)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_extract(uref->ubuf, offset, size, buffer);
}

/** @see ubuf_block_iovec_count */
static inline int uref_block_iovec_count(struct uref *uref,
                                         int offset, int size)
{
    if (uref->ubuf == NULL)
        return -1;
    return ubuf_block_iovec_count(uref->ubuf, offset, size);
}

/** @see ubuf_block_iovec_read */
static inline bool uref_block_iovec_read(struct uref *uref,
                                         int offset, int size,
                                         struct iovec *iovecs)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_iovec_read(uref->ubuf, offset, size, iovecs);
}

/** @see ubuf_block_iovec_unmap */
static inline bool uref_block_iovec_unmap(struct uref *uref,
                                          int offset, int size,
                                          struct iovec *iovecs)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_iovec_unmap(uref->ubuf, offset, size, iovecs);
}

/** @see ubuf_block_resize */
static inline bool uref_block_resize(struct uref *uref, int skip, int new_size)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_resize(uref->ubuf, skip, new_size);
}

/** @This allocates a new ubuf of size new_size, and copies part of the old
 * (possibly segemented) ubuf to the new one, switches the ubufs and frees
 * the old one.
 *
 * @param uref pointer to uref structure
 * @param ubuf_mgr management structure for the new ubuf
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @return false in case of error
 */
static inline bool uref_block_merge(struct uref *uref,
                                    struct ubuf_mgr *ubuf_mgr,
                                    int skip, int new_size)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_merge(ubuf_mgr, &uref->ubuf, skip, new_size);
}

/** @see ubuf_block_compare */
static inline bool uref_block_compare(struct uref *uref1, struct uref *uref2)
{
    if (uref1->ubuf == NULL || uref2->ubuf == NULL)
        return false;
    if (uref1->ubuf == uref2->ubuf)
        return true;
    return ubuf_block_compare(uref1->ubuf, uref2->ubuf);
}

/** @see ubuf_block_match */
static inline bool uref_block_match(struct uref *uref, const uint8_t *filter,
                                    const uint8_t *mask, size_t size)
{
    if (uref->ubuf == NULL)
        return false;
    return ubuf_block_match(uref->ubuf, filter, mask, size);
}

#endif

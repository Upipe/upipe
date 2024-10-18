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
 * @short Upipe wrapper for block ubuf and uref
 */

#ifndef _UPIPE_UREF_BLOCK_H_
/** @hidden */
#define _UPIPE_UREF_BLOCK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/uref.h"
#include "upipe/uref_attr.h"

#include <stdint.h>
#include <stdbool.h>
#include <sys/uio.h>

UREF_ATTR_VOID_UREF(block, start, UREF_FLAG_BLOCK_START, start of logical block)
UREF_ATTR_VOID_UREF(block, end, UREF_FLAG_BLOCK_END, end of logical block)
UREF_ATTR_UNSIGNED(block, header_size, "b.header", global headers size)
UREF_ATTR_UNSIGNED(block, net_ifindex, "b.ifindex", network interface index)
UREF_ATTR_SOCKADDR(block, net_srcaddr, "b.srcaddr", source address)
UREF_ATTR_SOCKADDR(block, net_ipi6_addr, "b.ipi6_addr", destination address when packet was received)

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
static inline int uref_block_size(struct uref *uref, size_t *size_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_size(uref->ubuf, size_p);
}

/** @see ubuf_block_size_linear */
static inline int uref_block_size_linear(struct uref *uref, int offset,
                                         size_t *size_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_size_linear(uref->ubuf, offset, size_p);
}

/** @see ubuf_block_read */
static inline int uref_block_read(struct uref *uref, int offset, int *size_p,
                                  const uint8_t **buffer_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_read(uref->ubuf, offset, size_p, buffer_p);
}

/** @see ubuf_block_write */
static inline int uref_block_write(struct uref *uref, int offset, int *size_p,
                                   uint8_t **buffer_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_write(uref->ubuf, offset, size_p, buffer_p);
}

/** @see ubuf_block_unmap */
static inline int uref_block_unmap(struct uref *uref, int offset)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_unmap(uref->ubuf, offset);
}

/** @see ubuf_block_insert */
static inline int uref_block_insert(struct uref *uref, int offset,
                                    struct ubuf *insert)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_insert(uref->ubuf, offset, insert);
}

/** @see ubuf_block_append */
static inline int uref_block_append(struct uref *uref, struct ubuf *append)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_append(uref->ubuf, append);
}

/** @see ubuf_block_delete */
static inline int uref_block_delete(struct uref *uref, int offset, int size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_delete(uref->ubuf, offset, size);
}

/** @see ubuf_block_truncate */
static inline int uref_block_truncate(struct uref *uref, int offset)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_truncate(uref->ubuf, offset);
}

/** @see ubuf_block_resize */
static inline int uref_block_resize(struct uref *uref, int skip, int new_size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_resize(uref->ubuf, skip, new_size);
}

/** @see ubuf_block_prepend */
static inline int uref_block_prepend(struct uref *uref, int prepend)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_prepend(uref->ubuf, prepend);
}

/** @see ubuf_block_splice */
static inline struct uref *uref_block_splice(struct uref *uref, int offset,
                                             int size)
{
    if (uref->ubuf == NULL)
        return NULL;
    struct uref *new_uref = uref_dup_inner(uref);
    if (unlikely(new_uref == NULL))
        return NULL;

    new_uref->ubuf = ubuf_block_splice(uref->ubuf, offset, size);
    if (unlikely(new_uref->ubuf == NULL)) {
        uref_free(new_uref);
        return NULL;
    }
    return new_uref;
}

/** @see ubuf_block_split */
static inline struct uref *uref_block_split(struct uref *uref, int offset)
{
    if (uref->ubuf == NULL)
        return NULL;
    struct uref *new_uref = uref_dup_inner(uref);
    if (unlikely(new_uref == NULL))
        return NULL;

    new_uref->ubuf = ubuf_block_split(uref->ubuf, offset);
    if (unlikely(new_uref->ubuf == NULL)) {
        uref_free(new_uref);
        return NULL;
    }
    return new_uref;
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
static inline int uref_block_peek_unmap(struct uref *uref, int offset,
                                        uint8_t *buffer,
                                        const uint8_t *read_buffer)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_peek_unmap(uref->ubuf, offset, buffer, read_buffer);
}

/** @see ubuf_block_extract */
static inline int uref_block_extract(struct uref *uref, int offset, int size,
                                     uint8_t *buffer)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_extract(uref->ubuf, offset, size, buffer);
}

/** @see ubuf_block_extract_bits */
static inline int uref_block_extract_bits(struct uref *uref,
        int offset, int size, struct ubits *bw)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_extract_bits(uref->ubuf, offset, size, bw);
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
static inline int uref_block_iovec_read(struct uref *uref, int offset, int size,
                                        struct iovec *iovecs)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_iovec_read(uref->ubuf, offset, size, iovecs);
}

/** @see ubuf_block_iovec_unmap */
static inline int uref_block_iovec_unmap(struct uref *uref,
                                         int offset, int size,
                                         struct iovec *iovecs)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_iovec_unmap(uref->ubuf, offset, size, iovecs);
}

/** @This allocates a new ubuf of size new_size, and copies part of the old
 * (possibly segmented) ubuf to the new one, switches the ubufs and frees
 * the old one.
 *
 * @param uref pointer to uref structure
 * @param ubuf_mgr management structure for the new ubuf
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @param new_size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @return an error code
 */
static inline int uref_block_merge(struct uref *uref, struct ubuf_mgr *ubuf_mgr,
                                   int skip, int new_size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_merge(ubuf_mgr, &uref->ubuf, skip, new_size);
}

/** @see ubuf_block_compare */
static inline int uref_block_compare(struct uref *uref, int offset,
                                     struct uref *uref_small)
{
    if (uref->ubuf == NULL || uref_small->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_compare(uref->ubuf, offset, uref_small->ubuf);
}

/** @see ubuf_block_equal */
static inline int uref_block_equal(struct uref *uref1, struct uref *uref2)
{
    if (uref1->ubuf == NULL || uref2->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_equal(uref1->ubuf, uref2->ubuf);
}

/** @see ubuf_block_match */
static inline int uref_block_match(struct uref *uref, const uint8_t *filter,
                                   const uint8_t *mask, size_t size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_match(uref->ubuf, filter, mask, size);
}

/** @see ubuf_block_scan */
static inline int uref_block_scan(struct uref *uref, size_t *offset_p,
                                  uint8_t word)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_scan(uref->ubuf, offset_p, word);
}

/** @see ubuf_block_find_va */
static inline int uref_block_find_va(struct uref *uref, size_t *offset_p,
                                     unsigned int nb_octets, va_list args)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_block_find_va(uref->ubuf, offset_p, nb_octets, args);
}

/** @see ubuf_block_find */
static inline int uref_block_find(struct uref *uref, size_t *offset_p,
                                  unsigned int nb_octets, ...)
{
    va_list args;
    va_start(args, nb_octets);
    int ret = uref_block_find_va(uref, offset_p, nb_octets, args);
    va_end(args);
    return ret;
}

#ifdef __cplusplus
}
#endif
#endif

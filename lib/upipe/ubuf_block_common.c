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

#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_common.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/** @This duplicates the content of the common structure for block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @return false in case of error
 */
bool ubuf_block_common_dup(struct ubuf *ubuf, struct ubuf *new_ubuf)
{
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    struct ubuf_block_common *new_common =
        ubuf_block_common_from_ubuf(new_ubuf);
    new_common->offset = common->offset;
    new_common->size = common->size;
    if (common->next_ubuf != NULL)
        if (unlikely((new_common->next_ubuf = ubuf_dup(common->next_ubuf))
                       == NULL))
            return false;
    return true;
}

/** @This returns the total size of the (segmented or not) ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param size_p reference written with the size of the buffer space if not NULL
 * @return false in case of error
 */
bool ubuf_block_common_size(struct ubuf *ubuf, size_t *size_p)
{
    if (likely(size_p != NULL)) {
        struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
        if (common->next_ubuf != NULL) {
            if (unlikely(!ubuf_block_size(common->next_ubuf, size_p)))
                return false;
        } else
            *size_p = 0;
        *size_p += common->size;
    }
    return true;
}

/** This template allows to declare functions handling control commands on
 * segmented blocks. */
#define UBUF_BLOCK_COMMON_TEMPLATE(name, ctype)                             \
/** @This checks whether a name command applies to the current block or the \
 * next one.                                                                \
 *                                                                          \
 * @param ubuf pointer to ubuf                                              \
 * @param offset offset of the buffer space wanted in the whole block, in   \
 * octets                                                                   \
 * @param size_p pointer to the size of the buffer space wanted, in octets, \
 * changed during execution for the actual readable size                    \
 * @param buffer_p reference written with a pointer to buffer space if not  \
 * NULL                                                                     \
 * @param handled_p reference written with true if the request was handled  \
 * by this function                                                         \
 * @return false in case of error                                           \
 */                                                                         \
bool ubuf_block_common_##name(struct ubuf *ubuf, int offset, int *size_p,   \
                              ctype **buffer_p, bool *handled_p)            \
{                                                                           \
    assert(offset >= 0);                                                    \
    assert(size_p != NULL && *size_p >= 0);                                 \
    assert(buffer_p != NULL);                                               \
                                                                            \
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);   \
    if (offset >= common->size) {                                           \
        *handled_p = true;                                                  \
        if (likely(common->next_ubuf != NULL))                              \
            return ubuf_block_##name(common->next_ubuf,                     \
                                     offset - common->size, size_p,         \
                                     buffer_p);                             \
        return false;                                                       \
    }                                                                       \
    *handled_p = false;                                                     \
    return true;                                                            \
}
UBUF_BLOCK_COMMON_TEMPLATE(read, const uint8_t)
UBUF_BLOCK_COMMON_TEMPLATE(write, uint8_t)
#undef UBUF_BLOCK_COMMON_TEMPLATE

/** @This checks whether an unmap command applies to the current block or the
 * next one.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets
 * @param size size of the buffer space wanted, in octets
 * @param handled_p reference written with true if the request was handled
 * by this function
 * @return false in case of error
 */
bool ubuf_block_common_unmap(struct ubuf *ubuf, int offset, int size,
                             bool *handled_p)
{
    assert(offset >= 0);
    assert(size >= 0);

    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    if (offset >= common->size) {
        *handled_p = true;
        if (likely(common->next_ubuf != NULL))
            return ubuf_block_unmap(common->next_ubuf, offset - common->size,
                                    size);
        return false;
    }
    *handled_p = false;
    return true;
}

/** @This applies an insert command.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets
 * @param insert pointer to inserted ubuf
 * @return false in case of error
 */
bool ubuf_block_common_insert(struct ubuf *ubuf, int offset,
                              struct ubuf *insert)
{
    assert(offset >= 0);
    assert(insert != NULL);

    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    if (offset < common->size) {
        struct ubuf *dup_ubuf;
        if (unlikely((dup_ubuf = ubuf_dup(ubuf)) == NULL))
            return false;
        if (unlikely(!ubuf_block_resize(dup_ubuf, offset, -1))) {
            ubuf_free(dup_ubuf);
            return false;
        }
        if (common->next_ubuf != NULL)
            ubuf_free(common->next_ubuf);

        common->size = offset;
        common->next_ubuf = dup_ubuf;
    }

    if (offset == common->size) {
        if (common->next_ubuf != NULL)
            ubuf_block_append(insert, common->next_ubuf);
        common->next_ubuf = insert;
        return true;
    }

    if (likely(common->next_ubuf != NULL))
        return ubuf_block_insert(common->next_ubuf, offset - common->size,
                                 insert);
    return false;
}

/** @This applies a delete command.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets
 * @param size size of the buffer space wanted, in octets
 * @return false in case of error
 */
bool ubuf_block_common_delete(struct ubuf *ubuf, int offset, int size)
{
    assert(offset >= 0);
    assert(size >= 0);

    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    /* Delete from the beginning */
    if (!offset) {
        size_t deleted = size <= common->size ? size : common->size;
        common->size -= deleted;
        common->offset += deleted;
        size -= deleted;
        if (!size)
            return true;
    }

    if (offset < common->size) {
        /* Delete from the end */
        if (offset + size >= common->size) {
            size_t deleted = common->size - offset;
            common->size -= deleted;
            size -= deleted;
            if (!size)
                return true;
        } else {
            struct ubuf *dup_ubuf;
            if (unlikely((dup_ubuf = ubuf_dup(ubuf)) == NULL))
                return false;
            if (unlikely(!ubuf_block_resize(dup_ubuf, offset + size, -1))) {
                ubuf_free(dup_ubuf);
                return false;
            }
            if (common->next_ubuf != NULL)
                ubuf_free(common->next_ubuf);

            common->size = offset;
            common->next_ubuf = dup_ubuf;
            return true;
        }
    }

    if (likely(common->next_ubuf != NULL))
        return ubuf_block_delete(common->next_ubuf, offset - common->size,
                                 size);
    return false;
}

/** @This walks down to the last segment and extends it. It doesn't deal
 * with prepend as it is supposed to be dealt with by the manager.
 *
 * @param ubuf pointer to ubuf
 * @param append number of octets to append to the last segment
 * @param handled_p reference written with true if the request was handled
 * by this function
 * @return false if an error occurred or the operation is not allowed
 */
bool ubuf_block_common_extend(struct ubuf *ubuf, int append, bool *handled_p)
{
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    if (common->next_ubuf == NULL) {
        *handled_p = false;
        return true;
    }

    *handled_p = true;
    size_t next_ubuf_size;
    if (unlikely(!ubuf_block_size(common->next_ubuf, &next_ubuf_size)))
        return false;
    return ubuf_block_resize(common->next_ubuf, 0, next_ubuf_size + append);
}

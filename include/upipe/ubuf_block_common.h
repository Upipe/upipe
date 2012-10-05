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
 * @short Upipe useful common definitions for block managers
 */

#ifndef _UPIPE_UBUF_BLOCK_COMMON_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_COMMON_H_

#include <upipe/ubase.h>
#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>

/** @This is a proposed common section of block ubuf, allowing to segment
 * data. In an opaque area you would typically store a pointer to shared
 * buffer space. */
struct ubuf_block_common {
    /** current offset of the data in the buffer */
    size_t offset;
    /** currently exported size of the buffer */
    size_t size;
    /** pointer to the ubuf containing the next segment of data */
    struct ubuf *next_ubuf;

    /** common structure */
    struct ubuf ubuf;
};

/** @This returns the high-level ubuf structure.
 *
 * @param common pointer to the ubuf_block_common structure
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *ubuf_block_common_to_ubuf(struct ubuf_block_common *common)
{
    return &common->ubuf;
}

/** @This returns the private ubuf_block_common structure.
 *
 * @param ubuf pointer to ubuf
 * @return pointer to the ubuf_block_common structure
 */
static inline struct ubuf_block_common *ubuf_block_common_from_ubuf(struct ubuf *ubuf)
{
    return container_of(ubuf, struct ubuf_block_common, ubuf);
}

/** @This initializes the common fields of a block ubuf.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_block_common_init(struct ubuf *ubuf)
{
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    common->offset = 0;
    common->size = 0;
    common->next_ubuf = NULL;
    uchain_init(&ubuf->uchain);
}

/** @This gets the offset and size members of the common structure for block
 * ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p reference to offset (may be NULL)
 * @param size_p reference to size (may be NULL)
 */
static inline void ubuf_block_common_get(struct ubuf *ubuf, size_t *offset_p,
                                         size_t *size_p)
{
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    if (offset_p != NULL)
        *offset_p = common->offset;
    if (size_p != NULL)
        *size_p = common->size;
}

/** @This sets the offset and size members of the common structure for block
 * ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset new offset
 * @param size new size
 */
static inline void ubuf_block_common_set(struct ubuf *ubuf, size_t offset,
                                         size_t size)
{
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    common->offset = offset;
    common->size = size;
}

/** @This frees the ubuf containg the next segments of the current ubuf.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_block_common_clean(struct ubuf *ubuf)
{
    struct ubuf_block_common *common = ubuf_block_common_from_ubuf(ubuf);
    if (common->next_ubuf != NULL)
        ubuf_free(common->next_ubuf);
}

/** @This duplicates the content of the common structure for block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @return false in case of error
 */
bool ubuf_block_common_dup(struct ubuf *ubuf, struct ubuf *new_ubuf);

/** @This returns the total size of the (segmented or not) ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param size_p reference written with the size of the buffer space if not NULL
 * @return false in case of error
 */
bool ubuf_block_common_size(struct ubuf *ubuf, size_t *size_p);

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
                              ctype **buffer_p, bool *handled_p);
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
                             bool *handled_p);

/** @This applies an insert command.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets
 * @param insert pointer to inserted ubuf
 * @return false in case of error
 */
bool ubuf_block_common_insert(struct ubuf *ubuf, int offset,
                              struct ubuf *insert);

/** @This applies a delete command.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets
 * @param size size of the buffer space wanted, in octets
 * @return false in case of error
 */
bool ubuf_block_common_delete(struct ubuf *ubuf, int offset, int size);

/** @This walks down to the last segment and extends it. It doesn't deal
 * with prepend as it is supposed to be dealt with by the manager.
 *
 * @param ubuf pointer to ubuf
 * @param append number of octets to append to the last segment
 * @param handled_p reference written with true if the request was handled
 * by this function
 * @return false if an error occurred or the operation is not allowed
 */
bool ubuf_block_common_extend(struct ubuf *ubuf, int append, bool *handled_p);

#endif

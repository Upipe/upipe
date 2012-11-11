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
 * @short Upipe buffer handling for block managers
 * This file defines the block-specific API to access buffers.
 */

#ifndef _UPIPE_UBUF_BLOCK_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_H_

#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/uio.h>

/** @This returns a new ubuf from a block allocator. This function shall not
 * create a segmented block.
 *
 * @param mgr management structure for this ubuf type
 * @param size size of the buffer
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_block_alloc(struct ubuf_mgr *mgr, int size)
{
    return ubuf_alloc(mgr, UBUF_ALLOC_BLOCK, size);
}

/** @This returns the size of the buffer pointed to by a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param size_p reference written with the size of the buffer space if not NULL
 * @return false in case of error
 */
static inline bool ubuf_block_size(struct ubuf *ubuf, size_t *size_p)
{
    return ubuf_control(ubuf, UBUF_SIZE_BLOCK, size_p);
}

/** @internal @This checks the offset and size parameters of a lot of functions,
 * and transforms them into absolute offset and size.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p reference to the offset of the buffer space wanted in the
 * whole block, in octets, negative values start from the end (may not be NULL)
 * @param size_p reference to the size of the buffer space wanted, in octets,
 * or -1 for the end of the block (may be NULL)
 * @return false when the parameters are invalid
 */
static inline bool ubuf_block_check_offset(struct ubuf *ubuf, int *offset_p,
                                           int *size_p)
{
    size_t ubuf_size;
    if (unlikely(!ubuf_block_size(ubuf, &ubuf_size) ||
                 *offset_p > (int)ubuf_size ||
                 (size_p != NULL && *offset_p + *size_p > (int)ubuf_size)))
        return false;
    if (*offset_p < 0)
        *offset_p += ubuf_size;
    if (size_p != NULL && *size_p == -1)
        *size_p = ubuf_size - *offset_p;
    return true;
}

/** @This returns a read-only pointer to the buffer space. You must call
 * @ref ubuf_block_unmap when you're done with the pointer.
 *
 * The size parameter must be inited with the desired size, or -1 for up to
 * the end of the buffer. However, if the block is segmented, it may be
 * decreased during execution.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size_p pointer to the size of the buffer space wanted, in octets,
 * or -1 for the end of the block, changed during execution for the actual
 * readable size
 * @param buffer_p reference written with a pointer to buffer space if not NULL
 * @return false in case of error
 */
static inline bool ubuf_block_read(struct ubuf *ubuf, int offset, int *size_p,
                                   const uint8_t **buffer_p)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, size_p)))
        return false;
    return ubuf_control(ubuf, UBUF_READ_BLOCK, offset, size_p, buffer_p);
}

/** @This returns a writable pointer to the buffer space, if the ubuf is not
 * shared. You must call @ref ubuf_block_unmap when you're done with the
 * pointer.
 *
 * The size parameter must be inited with the desired size, or -1 for up to
 * the end of the buffer. However, if the block is segmented, it may be
 * decreased during execution.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size_p pointer to the size of the buffer space wanted, in octets,
 * or -1 for the end of the block, changed during execution for the actual
 * @param buffer_p reference written with a pointer to buffer space if not NULL
 * @return false in case of error, or if the ubuf is shared
 */
static inline bool ubuf_block_write(struct ubuf *ubuf, int offset, int *size_p,
                                    uint8_t **buffer_p)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, size_p)))
        return false;
    return ubuf_control(ubuf, UBUF_WRITE_BLOCK, offset, size_p, buffer_p);
}

/** @This marks the buffer space as being currently unused, and the pointer
 * will be invalid until the next time the ubuf is mapped.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @return false in case of error
 */
static inline bool ubuf_block_unmap(struct ubuf *ubuf, int offset, int size)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return false;
    return ubuf_control(ubuf, UBUF_UNMAP_BLOCK, offset, size);
}

/** @This inserts a new ubuf inside a segmented-to-be block ubuf, at the given
 * position.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to insert the given ubuf.
 * @param insert pointer to ubuf to be inserted at the given offset; it must
 * no longer be used afterwards as it becomes included in the segmented ubuf
 * @return false in case of error
 */
static inline bool ubuf_block_insert(struct ubuf *ubuf, int offset,
                                     struct ubuf *insert)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, NULL)))
        return false;
    return ubuf_control(ubuf, UBUF_INSERT_BLOCK, offset, insert);
}

/** @This appends a new ubuf at the end of a segmented-to-be block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param append pointer to ubuf to be appended; it must no longer be used
 * afterwards as it becomes included in the segmented ubuf
 * @return false in case of error
 */
static inline bool ubuf_block_append(struct ubuf *ubuf, struct ubuf *append)
{
    size_t ubuf_size;
    if (unlikely(!ubuf_block_size(ubuf, &ubuf_size)))
        return false;
    return ubuf_block_insert(ubuf, ubuf_size, append);
}

/** @This deletes part of a ubuf. The ubuf may become segmented afterwards.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to delete data
 * @param size number of octets to delete
 * @return false in case of error
 */
static inline bool ubuf_block_delete(struct ubuf *ubuf, int offset, int size)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return false;
    return ubuf_control(ubuf, UBUF_DELETE_BLOCK, offset, size);
}

/** @This peeks into a ubuf for the given amount of octets, and returns a
 * read-only pointer to the buffer. If the buffer space wanted stretches
 * across two or more segments, it is copied to a (caller-supplied) memory
 * space, and a pointer to it is returned.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param buffer pointer to buffer space of at least size octets, only used
 * if the requested area stretches across two or more segments
 * @return pointer to buffer space, or NULL in case of error
 */
static inline const uint8_t *ubuf_block_peek(struct ubuf *ubuf,
                                             int offset, int size,
                                             uint8_t *buffer)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return NULL;

    int read_size = size;
    const uint8_t *read_buffer;
    if (unlikely(!ubuf_block_read(ubuf, offset, &read_size, &read_buffer)))
        return NULL;
    if (read_size == size)
        return read_buffer;

    uint8_t *write_buffer = buffer;
    for ( ; ; ) {
        memcpy(write_buffer, read_buffer, read_size);
        if (unlikely(!ubuf_block_unmap(ubuf, offset, read_size)))
            return NULL;
        size -= read_size;
        write_buffer += read_size;
        offset += read_size;
        read_size = size;
        if (size <= 0)
            break;

        if (unlikely(!ubuf_block_read(ubuf, offset, &read_size, &read_buffer)))
            return NULL;
    }
    return buffer;
}

/** @This unmaps the ubuf that's been peeked into, if necessary.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param buffer caller-supplied buffer space passed to @ref ubuf_block_peek
 * @param read_buffer buffer returned by @ref ubuf_block_peek
 * @return false in case of error
 */
static inline bool ubuf_block_peek_unmap(struct ubuf *ubuf,
                                         int offset, int size, uint8_t *buffer,
                                         const uint8_t *read_buffer)
{
    if (buffer == read_buffer)
        return true;

    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return false;
    return ubuf_block_unmap(ubuf, offset, size);
}

/** @This extracts a ubuf to an arbitrary memory space.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param buffer pointer to buffer space of at least size octets
 * @return false in case of error
 */
static inline bool ubuf_block_extract(struct ubuf *ubuf, int offset, int size,
                                      uint8_t *buffer)
{
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return false;

    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        if (unlikely(!ubuf_block_read(ubuf, offset, &read_size, &read_buffer)))
            return false;
        memcpy(buffer, read_buffer, read_size);
        if (unlikely(!ubuf_block_unmap(ubuf, offset, read_size)))
            return false;
        size -= read_size;
        buffer += read_size;
        offset += read_size;
    }
    return true;
}

/** @This returns the number of iovec needed to send part of a ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @return the number of iovec needed, or -1 in case of error
 */
static inline int ubuf_block_iovec_count(struct ubuf *ubuf,
                                         int offset, int size)
{
    int count = 0;
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return -1;

    while (size > 0) {
        int read_size = size;
        if (unlikely(!ubuf_block_read(ubuf, offset, &read_size, NULL) ||
                     !ubuf_block_unmap(ubuf, offset, read_size)))
            return -1;
        size -= read_size;
        offset += read_size;
        count++;
    }
    return count;
}

/** @This maps the requested part of a ubuf to the number of iovec given by
 * @ref ubuf_block_iovec_count.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param iovecs iovec structures array
 * @return false in case of error
 */
static inline bool ubuf_block_iovec_read(struct ubuf *ubuf,
                                         int offset, int size,
                                         struct iovec *iovecs)
{
    int count = 0;
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return false;

    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        if (unlikely(!ubuf_block_read(ubuf, offset, &read_size, &read_buffer)))
            return false;
        iovecs[count].iov_base = (void *)read_buffer;
        iovecs[count].iov_len = read_size;
        size -= read_size;
        offset += read_size;
        count++;
    }
    return true;
}

/** @This unmaps the parts of a ubuf previsouly mapped by @ref
 * ubuf_block_iovec_read.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param iovec iovec structures array
 * @return false in case of error
 */
static inline bool ubuf_block_iovec_unmap(struct ubuf *ubuf,
                                          int offset, int size,
                                          struct iovec *iovecs)
{
    int count = 0;
    if (unlikely(!ubuf_block_check_offset(ubuf, &offset, &size)))
        return false;

    while (size > 0) {
        if (unlikely(!ubuf_block_unmap(ubuf, offset, iovecs[count].iov_len)))
            return false;
        size -= iovecs[count].iov_len;
        offset += iovecs[count].iov_len;
        count++;
    }
    return true;
}

/** @internal @This checks the skip and new_size parameters of a lot of
 * resizing functions, and transforms them.
 *
 * @param ubuf pointer to ubuf
 * @param skip_p reference to number of octets to skip at the beginning of the
 * buffer (if < 0, extend buffer upwards)
 * @param new_size_p reference to final size of the buffer (if set to -1, keep
 * same buffer end)
 * @param ubuf_size_p filled in with the total size of the ubuf (may be NULL)
 * @return false when the parameters are invalid
 */
static inline bool ubuf_block_check_resize(struct ubuf *ubuf, int *skip_p,
                                           int *new_size_p, size_t *ubuf_size_p)
{
    size_t ubuf_size;
    if (unlikely(!ubuf_block_size(ubuf, &ubuf_size) ||
                 *skip_p > (int)ubuf_size))
        return false;
    if (*new_size_p == -1)
        *new_size_p = ubuf_size - *skip_p;
    if (unlikely(*new_size_p < -*skip_p))
        return false;
    if (ubuf_size_p != NULL)
        *ubuf_size_p = ubuf_size;
    return true;
}

/** @This resizes a block ubuf, if possible. This will only work if:
 * @list
 * @item the ubuf is only shrinked in one or both directions, or
 * @item the relevant low-level buffers are not shared with other ubuf and
 * the block manager allows to grow the buffer (ie. prepend/append have been
 * correctly specified at allocation, or reallocation is allowed)
 * @end list
 *
 * Should this fail, @ref ubuf_block_merge may be used to achieve the same
 * goal with an extra buffer copy.
 *
 * @param ubuf pointer to ubuf
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @param new_size final size of the buffer (if set to -1, keep same buffer
 * end)
 * @return false in case of error, or if the ubuf is shared, or if the operation
 * is not possible
 */
static inline bool ubuf_block_resize(struct ubuf *ubuf, int skip, int new_size)
{
    size_t ubuf_size;
    if (unlikely(!ubuf_block_check_resize(ubuf, &skip, &new_size, &ubuf_size)))
        return false;

    int prepend = 0, append = 0;
    if (skip < 0)
        prepend = -skip;
    if (new_size + skip > ubuf_size)
        append = new_size + skip - ubuf_size;

    if (prepend || append)
        if (!ubuf_control(ubuf, UBUF_EXTEND_BLOCK, prepend, append))
            return false;

    if (new_size + skip < ubuf_size)
        if (unlikely(!ubuf_block_delete(ubuf, new_size + skip - ubuf_size, -1)))
            goto ubuf_block_resize_err;
    if (skip > 0)
        if (unlikely(!ubuf_block_delete(ubuf, 0, skip)))
            goto ubuf_block_resize_err;
    return true;

ubuf_block_resize_err:
    /* It is very unlikely to go there */
    if (prepend)
        ubuf_block_delete(ubuf, 0, prepend);
    if (append)
        ubuf_block_delete(ubuf, -append, -1);
    return false;
}

/** @This copies part of a ubuf to a newly allocated ubuf.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf pointer to ubuf to copy
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @return pointer to newly allocated ubuf or NULL in case of error
 */
static inline struct ubuf *ubuf_block_copy(struct ubuf_mgr *mgr,
                                           struct ubuf *ubuf,
                                           int skip, int new_size)
{
    size_t ubuf_size;
    if (unlikely(!ubuf_block_check_resize(ubuf, &skip, &new_size, &ubuf_size)))
        return NULL;

    struct ubuf *new_ubuf = ubuf_block_alloc(mgr, new_size);
    if (unlikely(new_ubuf == NULL))
        return NULL;

    int extract_offset, extract_skip;
    if (skip < 0) {
        extract_offset = -skip;
        extract_skip = 0;
    } else {
        extract_offset = 0;
        extract_skip = skip;
    }
    int extract_size = new_size - extract_offset <= ubuf_size - extract_skip ?
                       new_size - extract_offset : ubuf_size - extract_skip;
    uint8_t *buffer;
    if (unlikely(!ubuf_block_write(new_ubuf, extract_offset, &extract_size,
                                   &buffer)))
        goto ubuf_block_copy_err;
    bool ret = ubuf_block_extract(ubuf, extract_skip, extract_size, buffer);
    if (unlikely(!ubuf_block_unmap(new_ubuf, extract_offset, extract_size) ||
                 !ret))
        goto ubuf_block_copy_err;
    return new_ubuf;

ubuf_block_copy_err:
    ubuf_free(new_ubuf);
    return NULL;
}

/** @This merges part of a (possibly segmented) ubuf to a newly allocated 
 * (non-segmented) ubuf, and replaces the old ubuf with the new ubuf.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf_p reference to a pointer to ubuf to replace with a non-segmented
 * block ubuf
 * @param skip number of octets to skip at the beginning of the buffer
 * (if < 0, extend buffer upwards)
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @return false in case of allocation error
 */
static inline bool ubuf_block_merge(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                                    int skip, int new_size)
{
    struct ubuf *new_ubuf = ubuf_block_copy(mgr, *ubuf_p, skip, new_size);
    if (unlikely(new_ubuf == NULL))
        return false;

    ubuf_free(*ubuf_p);
    *ubuf_p = new_ubuf;
    return true;
}

/** @This compares the content of two block ubufs.
 *
 * @param ubuf1 pointer to first ubuf
 * @param ubuf2 pointer to second ubuf
 * @return false if the ubufs are different
 */
static inline bool ubuf_block_compare(struct ubuf *ubuf1, struct ubuf *ubuf2)
{
    size_t ubuf1_size, ubuf2_size;
    if (unlikely(!ubuf_block_size(ubuf1, &ubuf1_size) ||
                 !ubuf_block_size(ubuf2, &ubuf2_size) ||
                 ubuf1_size != ubuf2_size))
        return false;

    int offset = 0;
    int size = ubuf1_size;
    while (size > 0) {
        int read_size1 = size, read_size2 = size;
        const uint8_t *read_buffer1, *read_buffer2;
        if (unlikely(!ubuf_block_read(ubuf1, offset, &read_size1,
                                      &read_buffer1)))
            return false;
        if (unlikely(!ubuf_block_read(ubuf2, offset, &read_size2,
                                      &read_buffer2))) {
            ubuf_block_unmap(ubuf1, offset, read_size1);
            return false;
        }
        int compare_size = read_size1 < read_size2 ? read_size1 : read_size2;
        bool ret = !memcmp(read_buffer1, read_buffer2, compare_size);
        ret = ubuf_block_unmap(ubuf1, offset, read_size1) && ret;
        ret = ubuf_block_unmap(ubuf2, offset, read_size2) && ret;
        if (!ret)
            return false;
        size -= compare_size;
        offset += compare_size;
    }
    return true;
}

/** @This checks if the beginning of a block ubuf matches a filter with a
 * given mask.
 *
 * @param ubuf pointer to ubuf
 * @param filter wanted content
 * @param mask mask of the bits to check
 * @param size size (in octets) of filter and mask
 * @return false if the ubuf doesn't match
 */
static inline bool ubuf_block_match(struct ubuf *ubuf, const uint8_t *filter,
                                    const uint8_t *mask, size_t size)
{
    size_t ubuf_size;
    if (unlikely(!ubuf_block_size(ubuf, &ubuf_size) || ubuf_size < size))
        return false;

    int offset = 0;
    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        if (unlikely(!ubuf_block_read(ubuf, offset, &read_size, &read_buffer)))
            return false;
        int compare_size = read_size < size ? read_size : size;
        bool ret = true;
        for (int i = 0; i < compare_size; i++)
            if ((read_buffer[i] & mask[offset + i]) != filter[offset + i]) {
                ret = false;
                break;
            }
        ret = ubuf_block_unmap(ubuf, offset, read_size) && ret;
        if (!ret)
            return false;
        size -= compare_size;
        offset += compare_size;
    }
    return true;
}

#endif

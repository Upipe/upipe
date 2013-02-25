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
 * @short Upipe buffer handling for block managers
 * This file defines the block-specific API to access buffers.
 */

#ifndef _UPIPE_UBUF_BLOCK_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_H_

#include <upipe/ubase.h>
#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/uio.h>
#include <assert.h>

/** @internal @This is a common section of block ubuf, allowing to segment
 * data. In an opaque area you would typically store a pointer to shared
 * buffer space. It is mandatory for block managers to include this
 * structure instead of @ref ubuf. */
struct ubuf_block {
    /** current offset of the data in the buffer */
    size_t offset;
    /** currently exported size of the buffer */
    size_t size;
    /** pointer to the ubuf containing the next segment of data */
    struct ubuf *next_ubuf;
    /** total size of the ubuf, including next segments */
    size_t total_size;

    /** true if UBUF_MAP_BLOCK & UBUF_UNMAP_BLOCK need to be called */
    bool map;
    /** mapped buffer */
    uint8_t *buffer;

    /** cached last ubuf */
    struct ubuf *cached_ubuf;
    /** cached last offset */
    size_t cached_offset;

    /** common structure */
    struct ubuf ubuf;
};

/** @internal @This returns the high-level ubuf structure.
 *
 * @param common pointer to the ubuf_block structure
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *ubuf_block_to_ubuf(struct ubuf_block *block)
{
    return &block->ubuf;
}

/** @internal @This returns the private ubuf_block structure.
 *
 * @param ubuf pointer to ubuf
 * @return pointer to the ubuf_block structure
 */
static inline struct ubuf_block *ubuf_block_from_ubuf(struct ubuf *ubuf)
{
    return container_of(ubuf, struct ubuf_block, ubuf);
}

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
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK))
        return false;
    if (likely(size_p != NULL)) {
        struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
        *size_p = block->total_size;
    }
    return true;
}

/** @internal @This returns the ubuf corresponding to the given offset.
 *
 * @param ubuf pointer to head ubuf
 * @param offset_p reference to the offset of the buffer space wanted in the
 * whole chain, in octets, negative values start from the end (may not be NULL),
 * filled in with the offset in the matched ubuf
 * @param size_p reference to the size of the buffer space wanted, in octets,
 * or -1 for the end of the block (may be NULL)
 * @return corresponding chained ubuf
 */
static inline struct ubuf *ubuf_block_get(struct ubuf *ubuf, int *offset_p,
                                          int *size_p)
{
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    struct ubuf_block *head_block = block;
    int saved_offset = *offset_p;

    if (*offset_p < 0)
        *offset_p += block->total_size;
    if (size_p != NULL && *size_p == -1)
        *size_p = block->total_size - *offset_p;

    if (block->cached_offset <= *offset_p) {
        *offset_p -= block->cached_offset;
        ubuf = block->cached_ubuf;
        block = ubuf_block_from_ubuf(ubuf);
    }

    unsigned int counter = 0;
    while (*offset_p >= block->size) {
        *offset_p -= block->size;
        ubuf = block->next_ubuf;
        if (unlikely(ubuf == NULL))
            return NULL;
        block = ubuf_block_from_ubuf(ubuf);
        counter++;
    }

    head_block->cached_ubuf = ubuf;
    head_block->cached_offset = saved_offset - *offset_p;
    return ubuf;
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
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, size_p)) == NULL))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (unlikely(block->map && !ubuf_control(ubuf, UBUF_MAP_BLOCK)))
        return false;

    if (buffer_p != NULL) {
        if (block->buffer == NULL)
            if (!ubuf_control(ubuf, UBUF_MAP_BLOCK, &block->buffer))
                return false;
        *buffer_p = block->buffer + block->offset + offset;
    }

    if (size_p != NULL && *size_p > block->size - offset)
        *size_p = block->size - offset;
    return true;
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
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, size_p)) == NULL))
        return false;

    if (!ubuf_control(ubuf, UBUF_SINGLE))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (unlikely(block->map && !ubuf_control(ubuf, UBUF_MAP_BLOCK)))
        return false;

    if (buffer_p != NULL) {
        if (block->buffer == NULL)
            if (!ubuf_control(ubuf, UBUF_MAP_BLOCK, &block->buffer))
                return false;
        *buffer_p = block->buffer + block->offset + offset;
    }

    if (size_p != NULL && *size_p > block->size - offset)
        *size_p = block->size - offset;
    return true;
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
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (block->map)
        return ubuf_control(ubuf, UBUF_UNMAP_BLOCK);
    return true;
}

/** @This appends a new ubuf at the end of a segmented-to-be block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param append pointer to ubuf to be appended; it must no longer be used
 * afterwards as it becomes included in the segmented ubuf
 */
static inline bool ubuf_block_append(struct ubuf *ubuf, struct ubuf *append)
{
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK ||
                 append->mgr->type != UBUF_ALLOC_BLOCK))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    struct ubuf_block *append_block = ubuf_block_from_ubuf(append);
    block->total_size += append_block->total_size;

    if (block->cached_ubuf != NULL) {
        ubuf = block->cached_ubuf;
        block = ubuf_block_from_ubuf(ubuf);
    }
    while (block->next_ubuf != NULL) {
        ubuf = block->next_ubuf;
        block = ubuf_block_from_ubuf(ubuf);
    }
    block->next_ubuf = append;
    return true;
}

/** @internal @This splits a block ubuf into two ubufs at the given offset.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to split
 * @return false in case of error
 */
static inline bool ubuf_block_split(struct ubuf *ubuf, int offset)
{
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    struct ubuf *next = block->next_ubuf;
    block->next_ubuf = NULL;

    struct ubuf *dup_ubuf;
    if (unlikely((dup_ubuf = ubuf_dup(ubuf)) == NULL)) {
        block->next_ubuf = next;
        return false;
    }

    struct ubuf_block *dup_block = ubuf_block_from_ubuf(dup_ubuf);
    dup_block->offset += offset;
    dup_block->size -= offset;
    dup_block->next_ubuf = next;

    block->size = offset;
    block->next_ubuf = dup_ubuf;
    return true;
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
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK ||
                 insert->mgr->type != UBUF_ALLOC_BLOCK))
        return false;

    struct ubuf_block *head_block = ubuf_block_from_ubuf(ubuf);
    if (unlikely((ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (offset < block->size)
        if (unlikely(!ubuf_block_split(ubuf, offset)))
            return false;

    struct ubuf_block *insert_block = ubuf_block_from_ubuf(insert);
    head_block->total_size += insert_block->total_size;

    if (block->next_ubuf != NULL)
        ubuf_block_append(insert, block->next_ubuf);
    block->next_ubuf = insert;
    return true;
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
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK))
        return false;

    struct ubuf_block *head_block = ubuf_block_from_ubuf(ubuf);
    if (unlikely((ubuf = ubuf_block_get(ubuf, &offset, &size)) == NULL))
        return false;
    int delete_size = size;

    do {
        struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
        if (!offset) {
            /* Delete from the beginning */
            size_t deleted = size <= block->size ? size : block->size;
            block->size -= deleted;
            block->offset += deleted;
            size -= deleted;
            if (!size)
                goto ubuf_block_delete_done;
        } else {
            /* Delete from the end */
            if (offset + size < block->size) {
                if (unlikely(!ubuf_block_split(ubuf, offset + size)))
                    return false;

                block->size = offset;
                goto ubuf_block_delete_done;
            }
            size_t deleted = block->size - offset;
            block->size -= deleted;
            size -= deleted;
            if (!size)
                goto ubuf_block_delete_done;
            offset = 0;
        }
        ubuf = block->next_ubuf;
    } while (ubuf != NULL);
    return false;

ubuf_block_delete_done:
    head_block->total_size -= delete_size;
    return true;
}

/** @This truncates a ubuf at a given offset, possibly releasing segments.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to truncate data
 * @return false in case of error
 */
static inline bool ubuf_block_truncate(struct ubuf *ubuf, int offset)
{
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK))
        return false;

    struct ubuf_block *head_block = ubuf_block_from_ubuf(ubuf);
    if (!offset) {
        if (head_block->next_ubuf != NULL) {
            ubuf_free(head_block->next_ubuf);
            head_block->next_ubuf = NULL;
        }
        head_block->size = 0;
        head_block->total_size = 0;
        head_block->cached_ubuf = &head_block->ubuf;
        head_block->cached_offset = 0;
        return true;
    }

    int saved_size = offset;
    offset--;
    if (unlikely((ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (block->next_ubuf != NULL) {
        ubuf_free(block->next_ubuf);
        block->next_ubuf = NULL;
    }
    block->size = offset + 1;
    head_block->total_size = saved_size;
    head_block->cached_ubuf = &head_block->ubuf;
    head_block->cached_offset = 0;
    return true;
}

/** @This shrinks a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param new_size final size of the buffer (if set to -1, keep same buffer
 * end)
 * @return false in case of error
 * is not possible
 */
static inline bool ubuf_block_resize(struct ubuf *ubuf, int offset,
                                     int new_size)
{
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (offset < 0)
        offset += block->total_size;
    if (unlikely(offset < 0))
        return false;

    if (new_size != -1) {
        if (new_size + offset > block->total_size)
            return false;
        if (new_size + offset < block->total_size)
            if (unlikely(!ubuf_block_truncate(ubuf, new_size + offset)))
                return false; /* should not happen */
    }

    if (offset > 0)
        return ubuf_block_delete(ubuf, 0, offset);
    return true;
}

/** @This extends a block ubuf, if possible. This will only work if
 * the relevant low-level buffers are not shared with other ubuf and
 * the block manager allows to grow the buffer (ie. prepend/append have been
 * correctly specified at allocation, or reallocation is allowed)
 *
 * Should this fail, @ref ubuf_block_merge may be used to achieve the same
 * goal with an extra buffer copy.
 *
 * @param ubuf pointer to ubuf
 * @param prepend number of octets to prepend
 * @param append number of octets to append
 * @return false in case of error, or if the ubuf is shared, or if the operation
 * is not possible
 */
static inline bool ubuf_block_extend(struct ubuf *ubuf, int prepend, int append)
{
    assert(prepend >= 0);
    assert(append >= 0);
    if (ubuf->mgr->type != UBUF_ALLOC_BLOCK || !ubuf_control(ubuf, UBUF_SINGLE))
        return false;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (prepend > block->offset)
        return false;
    if (append) {
        int offset = block->total_size - 1;
        struct ubuf *last_ubuf = offset != -1 ?
                                 ubuf_block_get(ubuf, &offset, NULL) : ubuf;
        struct ubuf_block *last_block = ubuf_block_from_ubuf(last_ubuf);
        if (unlikely(!ubuf_control(last_ubuf, UBUF_EXTEND_BLOCK,
                                   last_block->offset + last_block->size +
                                   append)))
            return false;
        last_block->size += append;
    }
    block->offset -= prepend;
    block->size += prepend;
    block->total_size += prepend + append;
    block->cached_offset += prepend;
    return true;
}

/** @This duplicates part of a ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param buffer pointer to buffer space of at least size octets
 * @return false in case of error
 */
static inline struct ubuf *ubuf_block_splice(struct ubuf *ubuf, int offset,
                                             int size)
{
    struct ubuf *new_ubuf;
    if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, &size)) == NULL ||
                 !ubuf_control(ubuf, UBUF_SPLICE_BLOCK, &new_ubuf,
                               offset, size)))
        return NULL;
    return new_ubuf;
}

/** @internal @This checks the offset and size parameters of a lot of functions,
 * and transforms them into absolute offset and size when needed.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p reference to the offset of the buffer space wanted in the
 * whole block, in octets, negative values start from the end (may not be NULL)
 * @param size_p reference to the size of the buffer space wanted, in octets,
 * or -1 for the end of the block (may not be NULL)
 * @return false when the parameters are invalid
 */
static inline bool ubuf_block_check_size(struct ubuf *ubuf, int *offset_p,
                                         int *size_p)
{
    if (*size_p == -1) {
        if (*offset_p < 0)
            *size_p = -*offset_p;
        else {
            if (unlikely(ubuf->mgr->type != UBUF_ALLOC_BLOCK))
                return false;

            struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
            *size_p = block->total_size - *offset_p;
        }
    }
    return true;
}

/** @This peeks into a ubuf for the given amount of octets, and returns a
 * read-only pointer to the buffer. If the buffer space wanted stretches
 * across two or more segments, it is copied to a (caller-supplied) memory
 * space, and a pointer to it is returned. It returns NULL if the ubuf isn't
 * large enough to provide enough data.
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
    if (unlikely(!ubuf_block_check_size(ubuf, &offset, &size)))
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
    if (unlikely(!ubuf_block_check_size(ubuf, &offset, &size)))
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
    if (unlikely(!ubuf_block_check_size(ubuf, &offset, &size)))
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
    if (unlikely(!ubuf_block_check_size(ubuf, &offset, &size)))
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
    if (unlikely(!ubuf_block_check_size(ubuf, &offset, &size)))
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

/** @This scans for an octet word in a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p start offset (in octets), written with the offset of the
 * first wanted word, or the total size of the ubuf if none was found
 * @param word word to scan for
 * @return false if the word wasn't found
 */
static inline bool ubuf_block_scan(struct ubuf *ubuf, size_t *offset_p,
                                   uint8_t word)
{
    const uint8_t *buffer;
    int size = -1;
    while (ubuf_block_read(ubuf, *offset_p, &size, &buffer)) {
        const void *match = memchr(buffer, word, size);
        if (match != NULL) {
            ubuf_block_unmap(ubuf, *offset_p, size);
            *offset_p += (const uint8_t *)match - buffer;
            return true;
        }
        ubuf_block_unmap(ubuf, *offset_p, size);
        *offset_p += size;
        size = -1;
    }
    return false;
}

/** @This finds a multi-octet word in a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p start offset (in octets), written with the offset of the
 * first wanted word, or first candidate if there aren't enough octets in the
 * ubuf, or the total size of the ubuf if none was found
 * @param nb_octets number of octets composing the word
 * @param args list of octets composing the word, in big-endian ordering
 * @return false if the word wasn't found
 */
static inline bool ubuf_block_find_va(struct ubuf *ubuf, size_t *offset_p,
                                      unsigned int nb_octets, va_list args)
{
    assert(nb_octets > 0);
    unsigned int sync = va_arg(args, unsigned int);
    if (nb_octets == 1)
        return ubuf_block_scan(ubuf, offset_p, sync);

    while (ubuf_block_scan(ubuf, offset_p, sync)) {
        uint8_t rbuffer[nb_octets - 1];
        const uint8_t *buffer = ubuf_block_peek(ubuf, *offset_p + 1,
                                                nb_octets - 1, rbuffer);
        if (buffer == NULL)
            return false;

        va_list args_copy;
        va_copy(args_copy, args);
        int i;
        for (i = 0; i < nb_octets - 1; i++) {
            unsigned int word = va_arg(args_copy, unsigned int);
            if (buffer[i] != word)
                break;
        }
        va_end(args_copy);
        ubuf_block_peek_unmap(ubuf, *offset_p + 1, nb_octets - 1, rbuffer,
                              buffer);
        if (i == nb_octets - 1)
            return true;
        (*offset_p)++;
    }
    return false;
}

/** @This finds a multi-octet word in a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p start offset (in octets), written with the offset of the
 * first wanted word, or first candidate if there aren't enough octets in the
 * ubuf, or the total size of the ubuf if none was found
 * @param nb_octets number of octets composing the word, followed by a list
 * of octets composing the word, in big-endian ordering
 * @return false if the word wasn't found
 */
static inline bool ubuf_block_find(struct ubuf *ubuf, size_t *offset_p,
                                   unsigned int nb_octets, ...)
{
    va_list args;
    va_start(args, nb_octets);
    bool ret = ubuf_block_find_va(ubuf, offset_p, nb_octets, args);
    va_end(args);
    return ret;
}

#endif

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
 * @short Upipe buffer handling for block managers
 * This file defines the block-specific API to access buffers.
 */

#ifndef _UPIPE_UBUF_BLOCK_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/uio.h>
#include <assert.h>

/** @This is a simple signature to make sure the ubuf_alloc internal API
 * is used properly. */
#define UBUF_ALLOC_BLOCK UBASE_FOURCC('b','l','c','k')

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

UBASE_FROM_TO(ubuf_block, ubuf, ubuf, ubuf)

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
 * @return an error code
 */
static inline int ubuf_block_size(struct ubuf *ubuf, size_t *size_p)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK))
        return UBASE_ERR_INVALID;
    if (likely(size_p != NULL)) {
        struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
        *size_p = block->total_size;
    }
    return UBASE_ERR_NONE;
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

    while (*offset_p >= block->size) {
        *offset_p -= block->size;
        ubuf = block->next_ubuf;
        if (unlikely(ubuf == NULL))
            return NULL;
        block = ubuf_block_from_ubuf(ubuf);
    }

    head_block->cached_ubuf = ubuf;
    head_block->cached_offset = saved_offset - *offset_p;
    return ubuf;
}

/** @This returns the size of the largest linear buffer that can be read at a
 * given offset.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size_p reference written with the size of the buffer space if not NULL
 * @return an error code
 */
static inline int ubuf_block_size_linear(struct ubuf *ubuf,
                                         int offset, size_t *size_p)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return UBASE_ERR_INVALID;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    *size_p = block->size - offset;
    return UBASE_ERR_NONE;
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
 * @return an error code
 */
static inline int ubuf_block_read(struct ubuf *ubuf, int offset,
                                  int *size_p, const uint8_t **buffer_p)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, size_p)) == NULL))
        return UBASE_ERR_INVALID;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (block->map) {
        UBASE_RETURN(ubuf_control(ubuf, UBUF_MAP_BLOCK, buffer_p))
    } else
        *buffer_p = block->buffer;
    *buffer_p += block->offset + offset;

    if (size_p != NULL && *size_p > block->size - offset)
        *size_p = block->size - offset;
    return UBASE_ERR_NONE;
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
 * @return an error code
 */
static inline int ubuf_block_write(struct ubuf *ubuf, int offset,
                                   int *size_p, uint8_t **buffer_p)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, size_p)) == NULL))
        return UBASE_ERR_INVALID;

    UBASE_RETURN(ubuf_control(ubuf, UBUF_SINGLE))

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (block->map) {
        UBASE_RETURN(ubuf_control(ubuf, UBUF_MAP_BLOCK, buffer_p))
    } else
        *buffer_p = block->buffer;
    *buffer_p += block->offset + offset;

    if (size_p != NULL && *size_p > block->size - offset)
        *size_p = block->size - offset;
    return UBASE_ERR_NONE;
}

/** @This marks the buffer space as being currently unused, and the pointer
 * will be invalid until the next time the ubuf is mapped.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @return an error code
 */
static inline int ubuf_block_unmap(struct ubuf *ubuf, int offset)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return UBASE_ERR_INVALID;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (block->map)
        return ubase_check(ubuf_control(ubuf, UBUF_UNMAP_BLOCK));
    return UBASE_ERR_NONE;
}

/** @This appends a new ubuf at the end of a segmented-to-be block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param append pointer to ubuf to be appended; it must no longer be used
 * afterwards as it becomes included in the segmented ubuf
 * @return an error code
 */
static inline int ubuf_block_append(struct ubuf *ubuf, struct ubuf *append)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 append->mgr->signature != UBUF_ALLOC_BLOCK))
        return UBASE_ERR_INVALID;

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
    return UBASE_ERR_NONE;
}

/** @internal @This splits a block ubuf into two ubufs at the given offset.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to split
 * @return an error code
 */
static inline int ubuf_block_split(struct ubuf *ubuf, int offset)
{
    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    struct ubuf *next = block->next_ubuf;
    block->next_ubuf = NULL;

    struct ubuf *dup_ubuf;
    if (unlikely((dup_ubuf = ubuf_dup(ubuf)) == NULL)) {
        block->next_ubuf = next;
        return UBASE_ERR_ALLOC;
    }

    struct ubuf_block *dup_block = ubuf_block_from_ubuf(dup_ubuf);
    dup_block->offset += offset;
    dup_block->size -= offset;
    dup_block->next_ubuf = next;

    block->size = offset;
    block->next_ubuf = dup_ubuf;
    return UBASE_ERR_NONE;
}

/** @This inserts a new ubuf inside a segmented-to-be block ubuf, at the given
 * position.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to insert the given ubuf.
 * @param insert pointer to ubuf to be inserted at the given offset; it must
 * no longer be used afterwards as it becomes included in the segmented ubuf
 * @return an error code
 */
static inline int ubuf_block_insert(struct ubuf *ubuf, int offset,
                                    struct ubuf *insert)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 insert->mgr->signature != UBUF_ALLOC_BLOCK))
        return UBASE_ERR_INVALID;

    struct ubuf_block *head_block = ubuf_block_from_ubuf(ubuf);
    if (unlikely((ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return UBASE_ERR_INVALID;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (offset < block->size) {
        UBASE_RETURN(ubuf_block_split(ubuf, offset))
    }

    struct ubuf_block *insert_block = ubuf_block_from_ubuf(insert);
    head_block->total_size += insert_block->total_size;

    if (block->next_ubuf != NULL)
        ubuf_block_append(insert, block->next_ubuf);
    block->next_ubuf = insert;
    return UBASE_ERR_NONE;
}

/** @This deletes part of a ubuf. The ubuf may become segmented afterwards.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to delete data
 * @param size number of octets to delete
 * @return an error code
 */
static inline int ubuf_block_delete(struct ubuf *ubuf, int offset, int size)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK))
        return UBASE_ERR_INVALID;

    struct ubuf_block *head_block = ubuf_block_from_ubuf(ubuf);
    if (unlikely((ubuf = ubuf_block_get(ubuf, &offset, &size)) == NULL))
        return UBASE_ERR_INVALID;
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
                if (unlikely(!ubase_check(ubuf_block_split(ubuf, offset + size))))
                    return UBASE_ERR_INVALID;

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
    return UBASE_ERR_INVALID;

ubuf_block_delete_done:
    head_block->total_size -= delete_size;
    return UBASE_ERR_NONE;
}

/** @This truncates a ubuf at a given offset, possibly releasing segments.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset at which to truncate data
 * @return an error code
 */
static inline int ubuf_block_truncate(struct ubuf *ubuf, int offset)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK))
        return UBASE_ERR_INVALID;

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
        return UBASE_ERR_NONE;
    }

    int saved_size = offset;
    offset--;
    if (unlikely((ubuf = ubuf_block_get(ubuf, &offset, NULL)) == NULL))
        return UBASE_ERR_INVALID;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (block->next_ubuf != NULL) {
        ubuf_free(block->next_ubuf);
        block->next_ubuf = NULL;
    }
    block->size = offset + 1;
    head_block->total_size = saved_size;
    head_block->cached_ubuf = &head_block->ubuf;
    head_block->cached_offset = 0;
    return UBASE_ERR_NONE;
}

/** @This shrinks a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param new_size final size of the buffer (if set to -1, keep same buffer
 * end)
 * @return an error code
 */
static inline int ubuf_block_resize(struct ubuf *ubuf, int offset, int new_size)
{
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK))
        return UBASE_ERR_INVALID;

    struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
    if (offset < 0)
        offset += block->total_size;
    if (unlikely(offset < 0))
        return UBASE_ERR_INVALID;

    if (new_size != -1) {
        if (new_size + offset > block->total_size)
            return UBASE_ERR_INVALID;
        if (new_size + offset < block->total_size) {
            UBASE_RETURN(ubuf_block_truncate(ubuf, new_size + offset))
        }
    }

    if (offset > 0)
        return ubuf_block_delete(ubuf, 0, offset);
    return UBASE_ERR_NONE;
}

/** @This duplicates part of a ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param buffer pointer to buffer space of at least size octets
 * @return newly allocated ubuf
 */
static inline struct ubuf *ubuf_block_splice(struct ubuf *ubuf, int offset,
                                             int size)
{
    struct ubuf *new_ubuf;
    if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK ||
                 (ubuf = ubuf_block_get(ubuf, &offset, &size)) == NULL ||
                 !ubase_check(ubuf_control(ubuf, UBUF_SPLICE_BLOCK,
                                           &new_ubuf, offset, size))))
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
 * @return an error code
 */
static inline int ubuf_block_check_size(struct ubuf *ubuf,
                                        int *offset_p, int *size_p)
{
    if (*size_p == -1) {
        if (*offset_p < 0)
            *size_p = -*offset_p;
        else {
            if (unlikely(ubuf->mgr->signature != UBUF_ALLOC_BLOCK))
                return UBASE_ERR_INVALID;

            struct ubuf_block *block = ubuf_block_from_ubuf(ubuf);
            *size_p = block->total_size - *offset_p;
        }
    }
    return UBASE_ERR_NONE;
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
    if (unlikely(!ubase_check(ubuf_block_check_size(ubuf, &offset, &size))))
        return NULL;

    int read_size = size;
    const uint8_t *read_buffer;
    if (unlikely(!ubase_check(ubuf_block_read(ubuf, offset,
                                              &read_size, &read_buffer))))
        return NULL;
    if (read_size == size)
        return read_buffer;

    uint8_t *write_buffer = buffer;
    for ( ; ; ) {
        memcpy(write_buffer, read_buffer, read_size);
        if (unlikely(!ubase_check(ubuf_block_unmap(ubuf, offset))))
            return NULL;
        size -= read_size;
        write_buffer += read_size;
        offset += read_size;
        read_size = size;
        if (size <= 0)
            break;

        if (unlikely(!ubase_check(ubuf_block_read(ubuf, offset,
                                                  &read_size, &read_buffer))))
            return NULL;
    }
    return buffer;
}

/** @This unmaps the ubuf that's been peeked into, if necessary.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param buffer caller-supplied buffer space passed to @ref ubuf_block_peek
 * @param read_buffer buffer returned by @ref ubuf_block_peek
 * @return an error code
 */
static inline int ubuf_block_peek_unmap(struct ubuf *ubuf,
                                        int offset, uint8_t *buffer,
                                        const uint8_t *read_buffer)
{
    if (buffer == read_buffer)
        return UBASE_ERR_NONE;

    return ubuf_block_unmap(ubuf, offset);
}

/** @This extracts a ubuf to an arbitrary memory space.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * octets, negative values start from the end
 * @param size size of the buffer space wanted, in octets, or -1 for the end
 * of the block
 * @param buffer pointer to buffer space of at least size octets
 * @return an error code
 */
static inline int ubuf_block_extract(struct ubuf *ubuf,
                                     int offset, int size, uint8_t *buffer)
{
    UBASE_RETURN(ubuf_block_check_size(ubuf, &offset, &size))

    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        UBASE_RETURN(ubuf_block_read(ubuf, offset, &read_size, &read_buffer))
        memcpy(buffer, read_buffer, read_size);
        UBASE_RETURN(ubuf_block_unmap(ubuf, offset))
        size -= read_size;
        buffer += read_size;
        offset += read_size;
    }
    return UBASE_ERR_NONE;
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
    if (unlikely(!ubase_check(ubuf_block_check_size(ubuf, &offset, &size))))
        return -1;

    while (size > 0) {
        size_t read_size;
        if (unlikely(!ubase_check(ubuf_block_size_linear(ubuf, offset,
                                                         &read_size))))
            return -1;
        if (read_size > size)
            read_size = size;
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
 * @return an error code
 */
static inline int ubuf_block_iovec_read(struct ubuf *ubuf,
                                        int offset, int size,
                                        struct iovec *iovecs)
{
    int count = 0;
    UBASE_RETURN(ubuf_block_check_size(ubuf, &offset, &size))

    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        UBASE_RETURN(ubuf_block_read(ubuf, offset, &read_size, &read_buffer))
        iovecs[count].iov_base = (void *)read_buffer;
        iovecs[count].iov_len = read_size;
        size -= read_size;
        offset += read_size;
        count++;
    }
    return UBASE_ERR_NONE;
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
 * @return an error code
 */
static inline int ubuf_block_iovec_unmap(struct ubuf *ubuf,
                                         int offset, int size,
                                         struct iovec *iovecs)
{
    int count = 0;
    UBASE_RETURN(ubuf_block_check_size(ubuf, &offset, &size))

    while (size > 0) {
        UBASE_RETURN(ubuf_block_unmap(ubuf, offset))
        size -= iovecs[count].iov_len;
        offset += iovecs[count].iov_len;
        count++;
    }
    return UBASE_ERR_NONE;
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
 * @return false in case of error
 */
static inline bool ubuf_block_check_resize(struct ubuf *ubuf, int *skip_p,
                                           int *new_size_p, size_t *ubuf_size_p)
{
    size_t ubuf_size;
    if (unlikely(!ubase_check(ubuf_block_size(ubuf, &ubuf_size)) ||
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
    bool ret;
    if (unlikely(!ubase_check(ubuf_block_write(new_ubuf, extract_offset,
                                               &extract_size, &buffer))))
        goto ubuf_block_copy_err;
    ret = ubase_check(ubuf_block_extract(ubuf, extract_skip, extract_size,
                                         buffer));
    if (unlikely(!ubase_check(ubuf_block_unmap(new_ubuf, extract_offset)) ||
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
 * @return an error code
 */
static inline int ubuf_block_merge(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                                   int skip, int new_size)
{
    struct ubuf *new_ubuf = ubuf_block_copy(mgr, *ubuf_p, skip, new_size);
    if (unlikely(new_ubuf == NULL))
        return UBASE_ERR_INVALID;

    ubuf_free(*ubuf_p);
    *ubuf_p = new_ubuf;
    return UBASE_ERR_NONE;
}

/** @This compares the content of a block ubuf in a larger ubuf.
 *
 * @param ubuf pointer to large ubuf
 * @param offset supposed offset of the small ubuf in the large ubuf
 * @param ubuf_small pointer to small ubuf
 * @return UBASE_ERR_NONE if the small ubuf matches the larger ubuf
 */
static inline int ubuf_block_compare(struct ubuf *ubuf, int offset,
                                     struct ubuf *ubuf_small)
{
    size_t ubuf_size, ubuf_size_small;
    UBASE_RETURN(ubuf_block_size(ubuf, &ubuf_size))
    UBASE_RETURN(ubuf_block_size(ubuf_small, &ubuf_size_small))
    if (ubuf_size < ubuf_size_small + offset)
        return UBASE_ERR_INVALID;

    int i = 0;
    int size = ubuf_size_small;
    while (size > 0) {
        int read_size = size, read_size_small = size;
        const uint8_t *read_buffer, *read_buffer_small;
        UBASE_RETURN(ubuf_block_read(ubuf, offset + i, &read_size,
                                     &read_buffer))
        if (unlikely(!ubase_check(ubuf_block_read(ubuf_small, i,
                            &read_size_small, &read_buffer_small)))) {
            ubuf_block_unmap(ubuf, offset + i);
            return UBASE_ERR_INVALID;
        }
        int compare_size = read_size < read_size_small ?
                           read_size : read_size_small;
        bool ret = !memcmp(read_buffer, read_buffer_small, compare_size);
        ubuf_block_unmap(ubuf, offset + i);
        ubuf_block_unmap(ubuf_small, i);
        if (!ret)
            return UBASE_ERR_INVALID;
        size -= compare_size;
        i += compare_size;
    }
    return UBASE_ERR_NONE;
}

/** @This compares whether two ubufs are identical.
 *
 * @param ubuf1 pointer to first ubuf
 * @param ubuf2 pointer to second ubuf
 * @return UBASE_ERR_NONE if the two ubufs are identical
 */
static inline int ubuf_block_equal(struct ubuf *ubuf1, struct ubuf *ubuf2)
{
    size_t ubuf_size1, ubuf_size2;
    UBASE_RETURN(ubuf_block_size(ubuf1, &ubuf_size1))
    UBASE_RETURN(ubuf_block_size(ubuf2, &ubuf_size2))
    if (ubuf_size1 != ubuf_size2)
        return UBASE_ERR_INVALID;

    return ubuf_block_compare(ubuf1, 0, ubuf2);
}

/** @This checks if the beginning of a block ubuf matches a filter with a
 * given mask.
 *
 * @param ubuf pointer to ubuf
 * @param filter wanted content
 * @param mask mask of the bits to check
 * @param size size (in octets) of filter and mask
 * @return UBASE_ERR_NONE if the ubuf matches
 */
static inline int ubuf_block_match(struct ubuf *ubuf, const uint8_t *filter,
                                   const uint8_t *mask, size_t size)
{
    size_t ubuf_size;
    UBASE_RETURN(ubuf_block_size(ubuf, &ubuf_size))
    if (ubuf_size < size)
        return UBASE_ERR_INVALID;

    int offset = 0;
    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        UBASE_RETURN(ubuf_block_read(ubuf, offset, &read_size, &read_buffer))
        int compare_size = read_size < size ? read_size : size;
        for (int i = 0; i < compare_size; i++)
            if ((read_buffer[i] & mask[offset + i]) != filter[offset + i]) {
                ubuf_block_unmap(ubuf, offset);
                return UBASE_ERR_INVALID;
            }
        UBASE_RETURN(ubuf_block_unmap(ubuf, offset))
        size -= compare_size;
        offset += compare_size;
    }
    return UBASE_ERR_NONE;
}

/** @This scans for an octet word in a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p start offset (in octets), written with the offset of the
 * first wanted word, or the total size of the ubuf if none was found
 * @param word word to scan for
 * @return UBASE_ERR_NONE if the word was found
 */
static inline int ubuf_block_scan(struct ubuf *ubuf, size_t *offset_p,
                                  uint8_t word)
{
    const uint8_t *buffer;
    int size = -1;
    for ( ; ; ) {
        UBASE_RETURN(ubuf_block_read(ubuf, *offset_p, &size, &buffer))
        const void *match = memchr(buffer, word, size);
        if (match != NULL) {
            ubuf_block_unmap(ubuf, *offset_p);
            *offset_p += (const uint8_t *)match - buffer;
            return UBASE_ERR_NONE;
        }
        ubuf_block_unmap(ubuf, *offset_p);
        *offset_p += size;
        size = -1;
    }
    return UBASE_ERR_INVALID;
}

/** @This finds a multi-octet word in a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p start offset (in octets), written with the offset of the
 * first wanted word, or first candidate if there aren't enough octets in the
 * ubuf, or the total size of the ubuf if none was found
 * @param nb_octets number of octets composing the word
 * @param args list of octets composing the word, in big-endian ordering
 * @return UBASE_ERR_NONE if the word was found
 */
static inline int ubuf_block_find_va(struct ubuf *ubuf, size_t *offset_p,
                                     unsigned int nb_octets, va_list args)
{
    assert(nb_octets > 0);
    unsigned int sync = va_arg(args, unsigned int);
    if (nb_octets == 1)
        return ubuf_block_scan(ubuf, offset_p, sync);

    for ( ; ; ) {
        UBASE_RETURN(ubuf_block_scan(ubuf, offset_p, sync))
        uint8_t rbuffer[nb_octets - 1];
        const uint8_t *buffer = ubuf_block_peek(ubuf, *offset_p + 1,
                                                nb_octets - 1, rbuffer);
        if (buffer == NULL)
            return UBASE_ERR_INVALID;

        va_list args_copy;
        va_copy(args_copy, args);
        int i;
        for (i = 0; i < nb_octets - 1; i++) {
            unsigned int word = va_arg(args_copy, unsigned int);
            if (buffer[i] != word)
                break;
        }
        va_end(args_copy);
        ubuf_block_peek_unmap(ubuf, *offset_p + 1, rbuffer, buffer);
        if (i == nb_octets - 1)
            return UBASE_ERR_NONE;
        (*offset_p)++;
    }
    return UBASE_ERR_INVALID;
}

/** @This finds a multi-octet word in a block ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset_p start offset (in octets), written with the offset of the
 * first wanted word, or first candidate if there aren't enough octets in the
 * ubuf, or the total size of the ubuf if none was found
 * @param nb_octets number of octets composing the word, followed by a list
 * of octets composing the word, in big-endian ordering
 * @return UBASE_ERR_NONE if the word was found
 */
static inline int ubuf_block_find(struct ubuf *ubuf, size_t *offset_p,
                                  unsigned int nb_octets, ...)
{
    va_list args;
    va_start(args, nb_octets);
    int err = ubuf_block_find_va(ubuf, offset_p, nb_octets, args);
    va_end(args);
    return err;
}

#ifdef __cplusplus
}
#endif
#endif

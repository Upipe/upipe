/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe buffer handling for sound managers
 * This file defines the sound-specific API to access buffers.
 */

#ifndef _UPIPE_UBUF_SOUND_H_
/** @hidden */
#define _UPIPE_UBUF_SOUND_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>

/** @This is a simple signature to make sure the ubuf_alloc internal API
 * is used properly. */
#define UBUF_ALLOC_SOUND UBASE_FOURCC('s','n','d',' ')

/** @This returns a new ubuf from a sound allocator.
 *
 * @param mgr management structure for this ubuf type
 * @param size size in samples
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_sound_alloc(struct ubuf_mgr *mgr, int size)
{
    return ubuf_alloc(mgr, UBUF_ALLOC_SOUND, size);
}

/** @This returns the size of the sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param size_p reference written with the number of samples of the sound
 * if not NULL
 * @param sample_size_p reference written with the number of octets in a
 * sample of a plane if not NULL
 * @return an error code
 */
static inline int ubuf_sound_size(struct ubuf *ubuf, size_t *size_p,
                                  uint8_t *sample_size_p)
{
    return ubuf_control(ubuf, UBUF_SIZE_SOUND, size_p, sample_size_p);
}

/** @This iterates on sound planes channel types. Start by initializing
 * *channel_p to NULL. If *channel_p is NULL after running this function, there
 * are no more planes in this sound. Otherwise the string pointed to by
 * *channel_p remains valid until the ubuf sound manager is deallocated.
 *
 * @param ubuf pointer to ubuf
 * @param channel_p reference written with channel type of the next plane
 * @return an error code
 */
static inline int ubuf_sound_plane_iterate(struct ubuf *ubuf,
                                           const char **channel_p)
{
    return ubuf_control(ubuf, UBUF_ITERATE_SOUND_PLANE, channel_p);
}

/** @This marks the buffer space as being currently unused, and the pointer
 * will be invalid until the next time the ubuf is mapped.
 *
 * @param ubuf pointer to ubuf
 * @param channel channel type (see channel reference)
 * @param offset offset of the sound area wanted in the whole
 * sound, negative values start from the end, in samples
 * @param size number of samples wanted, or -1 for until the end
 * @return an error code
 */
static inline int ubuf_sound_plane_unmap(struct ubuf *ubuf, const char *channel,
                                         int offset, int size)
{
    return ubuf_control(ubuf, UBUF_UNMAP_SOUND_PLANE, channel, offset, size);
}

/** @This marks the buffer space as being currently unused, and pointers
 * will be invalid until the next time the ubuf is mapped.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the sound area wanted in the whole
 * sound, negative values start from the end, in samples
 * @param size number of samples wanted, or -1 for until the end
 * @param planes number of planes mapped
 * @return an error code
 */
static inline int ubuf_sound_unmap(struct ubuf *ubuf, int offset, int size,
                                   uint8_t planes)
{
    const char *channel = NULL;
    unsigned int i = 0;
    bool ret = true;
    while (i < planes &&
           ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&
           channel != NULL) {
        ret = ubase_check(ubuf_sound_plane_unmap(ubuf, channel, offset,
                                                 size)) && ret;
        i++;
    }
    return ret ? UBASE_ERR_NONE : UBASE_ERR_INVALID;
}

#define UBUF_SOUND_MAP_TEMPLATE(type, desc)                                 \
/** @This returns a read-only pointer to the buffer space as desc.          \
 * You must call @ref ubuf_sound_plane_unmap when you're done with          \
 * the pointer.                                                             \
 *                                                                          \
 * @param ubuf pointer to ubuf                                              \
 * @param channel channel type (see channel reference)                      \
 * @param offset offset of the sound area wanted in the whole               \
 * sound, negative values start from the end, in samples                    \
 * @param size number of samples wanted, or -1 for until the end            \
 * @param buffer_p reference written with a pointer to buffer space if not  \
 * NULL                                                                     \
 * @return an error code                                                    \
 */                                                                         \
static inline int ubuf_sound_plane_read_##type(struct ubuf *ubuf,           \
        const char *channel, int offset, int size, const type **buffer_p)   \
{                                                                           \
    return ubuf_control(ubuf, UBUF_READ_SOUND_PLANE, channel,               \
                        offset, size, (const uint8_t **)buffer_p);          \
}                                                                           \
/** @This returns a writable pointer to the buffer space, if the ubuf is not\
 * shared, as desc. You must call @ref ubuf_sound_plane_unmap when you're   \
 * done with the pointer.                                                   \
 *                                                                          \
 * @param ubuf pointer to ubuf                                              \
 * @param channel channel type (see channel reference)                      \
 * @param offset offset of the sound area wanted in the whole               \
 * sound, negative values start from the end, in samples                    \
 * @param size number of samples wanted, or -1 for until the end            \
 * @param buffer_p reference written with a pointer to buffer space if not  \
 * NULL                                                                     \
 * @return an error code                                                    \
 */                                                                         \
static inline int ubuf_sound_plane_write_##type(struct ubuf *ubuf,          \
        const char *channel, int offset, int size, type **buffer_p)         \
{                                                                           \
    return ubuf_control(ubuf, UBUF_WRITE_SOUND_PLANE, channel,              \
                        offset, size, (uint8_t **)buffer_p);                \
}                                                                           \
/** @This returns read-only pointers to the buffer spaces as desc. The      \
 * planes are iterated in allocation order.                                 \
 * You must call @ref ubuf_sound_unmap when you're done with                \
 * the pointers.                                                            \
 *                                                                          \
 * @param ubuf pointer to ubuf                                              \
 * @param offset offset of the sound area wanted in the whole               \
 * sound, negative values start from the end, in samples                    \
 * @param size number of samples wanted, or -1 for until the end            \
 * @param buffer_p array of references written with pointers to buffer space\
 * @param planes number of elements allocated in array buffers_p            \
 * @return an error code                                                    \
 */                                                                         \
static inline int ubuf_sound_read_##type(struct ubuf *ubuf,                 \
        int offset, int size, const type *buffers_p[], uint8_t planes)      \
{                                                                           \
    const char *channel = NULL;                                             \
    unsigned int i = 0;                                                     \
    while (i < planes &&                                                    \
           ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&         \
           channel != NULL) {                                               \
        if (unlikely(!ubase_check(ubuf_sound_plane_read_##type(ubuf,        \
                            channel, offset, size, &buffers_p[i])))) {      \
            ubuf_sound_unmap(ubuf, offset, size, i);                        \
            return UBASE_ERR_INVALID;                                       \
        }                                                                   \
        i++;                                                                \
    }                                                                       \
    while (i < planes)                                                      \
        buffers_p[i++] = NULL;                                              \
    return UBASE_ERR_NONE;                                                  \
}                                                                           \
/** @This returns writable pointers to the buffer spaces as desc. The       \
 * planes are iterated in allocation order.                                 \
 * You must call @ref ubuf_sound_unmap when you're done with                \
 * the pointers.                                                            \
 *                                                                          \
 * @param ubuf pointer to ubuf                                              \
 * @param offset offset of the sound area wanted in the whole               \
 * sound, negative values start from the end, in samples                    \
 * @param size number of samples wanted, or -1 for until the end            \
 * @param buffer_p array of references written with pointers to buffer space\
 * @param planes number of elements allocated in array buffers_p            \
 * @return an error code                                                    \
 */                                                                         \
static inline int ubuf_sound_write_##type(struct ubuf *ubuf,                \
        int offset, int size, type *buffers_p[], uint8_t planes)            \
{                                                                           \
    const char *channel = NULL;                                             \
    unsigned int i = 0;                                                     \
    while (i < planes &&                                                    \
           ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&         \
           channel != NULL) {                                               \
        if (unlikely(!ubase_check(ubuf_sound_plane_write_##type(ubuf,       \
                            channel, offset, size, &buffers_p[i])))) {      \
            ubuf_sound_unmap(ubuf, offset, size, i);                        \
            return UBASE_ERR_INVALID;                                       \
        }                                                                   \
        i++;                                                                \
    }                                                                       \
    while (i < planes)                                                      \
        buffers_p[i++] = NULL;                                              \
    return UBASE_ERR_NONE;                                                  \
}

UBUF_SOUND_MAP_TEMPLATE(void, void objects)
UBUF_SOUND_MAP_TEMPLATE(uint8_t, 8-bit unsigned integers)
UBUF_SOUND_MAP_TEMPLATE(int16_t, 16-bit signed integers)
UBUF_SOUND_MAP_TEMPLATE(int32_t, 32-bit signed integers)
UBUF_SOUND_MAP_TEMPLATE(float, floats)
UBUF_SOUND_MAP_TEMPLATE(double, double-precision floats)
#undef UBUF_SOUND_MAP_TEMPLATE

/** @This shrinks a sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * samples, negative values start from the end
 * @param new_size final size of the buffer, in samples (if set
 * to -1, keep same end)
 * @return an error code
 */
static inline int ubuf_sound_resize(struct ubuf *ubuf, int offset, int new_size)
{
    return ubuf_control(ubuf, UBUF_RESIZE_SOUND, offset, new_size);
}

/** @This copies a sound ubuf to a newly allocated ubuf, and doesn't deal
 * with the old ubuf or a dictionary.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf pointer to ubuf to copy
 * @param skip number of samples to skip at the beginning of each plane (if < 0,
 * extend the sound upwards)
 * @param new_size final size of the buffer, in samples (if set
 * to -1, keep same end)
 * @return pointer to newly allocated ubuf or NULL in case of error
 */
static inline struct ubuf *ubuf_sound_copy(struct ubuf_mgr *mgr,
                                           struct ubuf *ubuf,
                                           int skip, int new_size)
{
    size_t ubuf_size;
    uint8_t sample_size;
    if (unlikely(!ubase_check(ubuf_sound_size(ubuf, &ubuf_size, &sample_size))
                 || skip >= (int)ubuf_size))
        return NULL;
    if (new_size == -1)
        new_size = ubuf_size - skip;

    if (unlikely(skip + new_size <= 0)) {
        return NULL;
    }

    struct ubuf *new_ubuf = ubuf_sound_alloc(mgr, new_size);
    if (unlikely(new_ubuf == NULL))
        return NULL;

    uint8_t new_sample_size;
    int extract_offset, extract_skip;
    int extract_size;
    const char *channel = NULL;
    if (unlikely(!ubase_check(ubuf_sound_size(new_ubuf, NULL,
                                              &new_sample_size)) ||
                 new_sample_size != sample_size))
        goto ubuf_sound_copy_err;

    if (skip < 0) {
        extract_offset = -skip;
        extract_skip = 0;
    } else {
        extract_offset = 0;
        extract_skip = skip;
    }
    extract_size =
        new_size - extract_offset <= (int)ubuf_size - extract_skip ?
        new_size - extract_offset : (int)ubuf_size - extract_skip;

    while (ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&
           channel != NULL) {
        uint8_t *new_buffer;
        const uint8_t *buffer;
        if (unlikely(!ubase_check(ubuf_sound_plane_write_uint8_t(new_ubuf,
                                           channel,
                                           extract_offset, extract_size,
                                           &new_buffer))))
            goto ubuf_sound_copy_err;
        if (unlikely(!ubase_check(ubuf_sound_plane_read_uint8_t(ubuf, channel,
                                          extract_skip, extract_size,
                                          &buffer)))) {
            ubuf_sound_plane_unmap(new_ubuf, channel,
                                   extract_offset, extract_size);
            goto ubuf_sound_copy_err;
        }

        memcpy(new_buffer, buffer, extract_size * sample_size);

        bool ret = ubase_check(ubuf_sound_plane_unmap(new_ubuf, channel,
                                        extract_offset, extract_size));
        if (unlikely(!ubase_check(ubuf_sound_plane_unmap(ubuf, channel,
                                           extract_skip, extract_size)) ||
                     !ret))
            goto ubuf_sound_copy_err;
    }
    return new_ubuf;

ubuf_sound_copy_err:
    ubuf_free(new_ubuf);
    return NULL;
}


/** @This interleaves planar formats to a user-allocated buffer.
 *
 * @param ubuf pointer to ubuf
 * @param offset read offset in samples
 * @param samples number of samples to interleave
 * @param sample_size sample size
 * @param planes number of planes to interleave
 * @return an error code
 */
static inline int ubuf_sound_interleave(struct ubuf *ubuf, uint8_t *buf,
                                        int offset, int samples,
                                        uint8_t sample_size, uint8_t planes)
{
    int i, j, k;
    const uint8_t *buffers_p[planes];
    UBASE_RETURN(ubuf_sound_read_uint8_t(ubuf, offset, samples, buffers_p, planes));
    for (i = 0; i < samples; i++) {
        for (j = 0; j < planes; j++) {
            if (unlikely(buffers_p[j] == NULL))
                return UBASE_ERR_INVALID;
            for (k = 0; k < sample_size; k++) {
                *buf++ = buffers_p[j][i * sample_size + k];
            }
        }
    }
    UBASE_RETURN(ubuf_sound_unmap(ubuf, offset, samples, planes));

    return UBASE_ERR_NONE;
}

/** @This copies part of a ubuf to a newly allocated ubuf, and replaces the
 * old ubuf with the new ubuf.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf_p reference to a pointer to ubuf to replace with a new sound
 * ubuf
 * @param skip number of samples to skip at the beginning of each plane (if < 0,
 * extend the sound upwards)
 * @param new_size final size of the buffer, in samples (if set
 * to -1, keep same end)
 * @return an error code
 */
static inline int ubuf_sound_replace(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                                     int skip, int new_size)
{
    struct ubuf *new_ubuf = ubuf_sound_copy(mgr, *ubuf_p, skip, new_size);
    if (unlikely(new_ubuf == NULL))
        return UBASE_ERR_ALLOC;

    ubuf_free(*ubuf_p);
    *ubuf_p = new_ubuf;
    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

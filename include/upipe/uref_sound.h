/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe wrapper for sound ubuf and uref
 */

#ifndef _UPIPE_UREF_SOUND_H_
/** @hidden */
#define _UPIPE_UREF_SOUND_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>

#include <stdint.h>

/** @This returns a new uref pointing to a new ubuf pointing to a sound.
 * This is equivalent to the two operations sequentially, and is a shortcut.
 *
 * @param uref_mgr management structure for this uref type
 * @param ubuf_mgr management structure for this ubuf type
 * @param size size in samples
 * @return pointer to uref or NULL in case of failure
 */
static inline struct uref *uref_sound_alloc(struct uref_mgr *uref_mgr,
                                            struct ubuf_mgr *ubuf_mgr,
                                            int size)
{
    struct uref *uref = uref_alloc(uref_mgr);
    if (unlikely(uref == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_sound_alloc(ubuf_mgr, size);
    if (unlikely(ubuf == NULL)) {
        uref_free(uref);
        return NULL;
    }

    uref_attach_ubuf(uref, ubuf);
    return uref;
}

/** @see ubuf_sound_size */
static inline int uref_sound_size(struct uref *uref, size_t *size_p,
                                  uint8_t *sample_size_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_size(uref->ubuf, size_p, sample_size_p);
}

/** @see ubuf_sound_plane_iterate */
static inline int uref_sound_plane_iterate(struct uref *uref,
                                           const char **channel_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_plane_iterate(uref->ubuf, channel_p);
}

/** @see ubuf_sound_plane_unmap */
static inline int uref_sound_plane_unmap(struct uref *uref, const char *channel,
                                         int offset, int size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_plane_unmap(uref->ubuf, channel, offset, size);
}

/** @see ubuf_sound_unmap */
static inline int uref_sound_unmap(struct uref *uref, int offset, int size,
                                   uint8_t planes)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_unmap(uref->ubuf, offset, size, planes);
}

#define UREF_SOUND_MAP_TEMPLATE(type)                                       \
/** @see ubuf_sound_plane_read_##type */                                    \
static inline int uref_sound_plane_read_##type(struct uref *uref,           \
        const char *channel, int offset, int size, const type **buffer_p)   \
{                                                                           \
    if (uref->ubuf == NULL)                                                 \
        return UBASE_ERR_INVALID;                                           \
    return ubuf_sound_plane_read_##type(uref->ubuf, channel, offset, size,  \
                                        buffer_p);                          \
}                                                                           \
/** @see ubuf_sound_plane_write_##type */                                   \
static inline int uref_sound_plane_write_##type(struct uref *uref,          \
        const char *channel, int offset, int size, type **buffer_p)         \
{                                                                           \
    if (uref->ubuf == NULL)                                                 \
        return UBASE_ERR_INVALID;                                           \
    return ubuf_sound_plane_write_##type(uref->ubuf, channel, offset, size, \
                                         buffer_p);                         \
}                                                                           \
/** @see ubuf_sound_read_##type */                                          \
static inline int uref_sound_read_##type(struct uref *uref,                 \
        int offset, int size, const type *buffers_p[], uint8_t planes)      \
{                                                                           \
    if (uref->ubuf == NULL)                                                 \
        return UBASE_ERR_INVALID;                                           \
    return ubuf_sound_read_##type(uref->ubuf, offset, size, buffers_p,      \
                                  planes);                                  \
}                                                                           \
/** @see ubuf_sound_write_##type */                                         \
static inline int uref_sound_write_##type(struct uref *uref,                \
        int offset, int size, type *buffers_p[], uint8_t planes)            \
{                                                                           \
    if (uref->ubuf == NULL)                                                 \
        return UBASE_ERR_INVALID;                                           \
    return ubuf_sound_write_##type(uref->ubuf, offset, size, buffers_p,     \
                                   planes);                                 \
}

UREF_SOUND_MAP_TEMPLATE(void)
UREF_SOUND_MAP_TEMPLATE(uint8_t)
UREF_SOUND_MAP_TEMPLATE(int16_t)
UREF_SOUND_MAP_TEMPLATE(int32_t)
UREF_SOUND_MAP_TEMPLATE(float)
UREF_SOUND_MAP_TEMPLATE(double)
#undef UREF_SOUND_MAP_TEMPLATE

/** @see ubuf_sound_resize */
static inline int uref_sound_resize(struct uref *uref, int skip, int new_size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_resize(uref->ubuf, skip, new_size);
}

/** @see ubuf_sound_interleave */
static inline int uref_sound_interleave(struct uref *uref, uint8_t *buf,
                                        int offset, int samples,
                                        uint8_t sample_size, uint8_t planes)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_interleave(uref->ubuf, buf, offset,
                                 samples, sample_size, planes);
}

/** @This allocates a new ubuf of size new_size, and copies part of
 * the old sound ubuf to the new one, switches the ubufs and frees
 * the old one.
 *
 * @param uref pointer to uref structure
 * @param ubuf_mgr management structure for the new ubuf
 * @param skip number of samples to skip at the beginning of each plane (if < 0,
 * extend the sound upwards)
 * @param new_size final size of the buffer, in samples (if set
 * to -1, keep same end)
 * @return an error code
 */
static inline int uref_sound_replace(struct uref *uref,
        struct ubuf_mgr *ubuf_mgr, int skip, int new_size)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_sound_replace(ubuf_mgr, &uref->ubuf, skip, new_size);
}

/** @internal @This consumes samples off a sound uref, and adjusts dates
 * and duration accordingly.
 *
 * @param uref uref structure
 * @param consume number of samples to consume from uref
 * @param samplerate sample rate
 * @return an error code
 */
static inline int uref_sound_consume(struct uref *uref, size_t consume,
                                     uint64_t samplerate)
{
    UBASE_RETURN(uref_sound_resize(uref, consume, -1))

    size_t size;
    uref_sound_size(uref, &size, NULL);

    uint64_t duration = (uint64_t)size * UCLOCK_FREQ / samplerate;
    uref_clock_set_duration(uref, duration);

    uint64_t ts_offset = (uint64_t)consume * UCLOCK_FREQ / samplerate;
    uint64_t date;
    int type;
    uref_clock_get_date_prog(uref, &date, &type);
    if (type != UREF_DATE_NONE)
        uref_clock_set_date_prog(uref, date + ts_offset, type);
    uref_clock_get_date_sys(uref, &date, &type);
    if (type != UREF_DATE_NONE)
        uref_clock_set_date_sys(uref, date + ts_offset, type);
    uref_clock_get_date_orig(uref, &date, &type);
    if (type != UREF_DATE_NONE)
        uref_clock_set_date_orig(uref, date + ts_offset, type);

    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

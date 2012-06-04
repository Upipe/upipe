/*****************************************************************************
 * uref_sound.h: sound semantics for uref and ubuf structures
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

#ifndef _UPIPE_UREF_SOUND_H_
/** @hidden */
#define _UPIPE_UREF_SOUND_H_

#include <stdint.h>
#include <assert.h>

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>

UREF_ATTR_TEMPLATE(sound, offset, "s.offset", unsigned, uint64_t, sound offset)
UREF_ATTR_TEMPLATE(sound, samples, "s.samples", unsigned, uint64_t, number of samples)

/** @This returns a new uref pointing to a new ubuf pointing to a sound.
 * This is equivalent to the two operations sequentially, and is a shortcut.
 *
 * @param uref_mgr management structure for this uref pool
 * @param ubuf_mgr management structure for this ubuf pool
 * @param samples number of samples per channel
 * @return pointer to uref or NULL in case of failure
 */
static inline struct uref *uref_sound_alloc(struct uref_mgr *uref_mgr,
                                            struct ubuf_mgr *ubuf_mgr,
                                            size_t samples)
{
    struct uref *uref = uref_ubuf_alloc(uref_mgr, ubuf_mgr,
                                        UBUF_ALLOC_TYPE_SOUND, samples);
    if (unlikely(uref == NULL)) return NULL;

    if (unlikely(!uref_sound_set_samples(&uref, samples))) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

/** @internal @This returns a pointer to the buffer space.
 *
 * @param uref struct uref structure
 * @param samples_p reference written with the number of samples per channel
 * @return pointer to buffer space or NULL in case of error
 */
static inline uint8_t *uref_sound_buffer(struct uref *uref, size_t *samples_p)
{
    if (unlikely(uref->ubuf == NULL)) {
        *samples_p = 0;
        return NULL;
    }

    uint64_t offset = 0;
    uref_sound_get_offset(uref, &offset);
    if (likely(samples_p != NULL)) {
        uint64_t samples = 0;
        uref_sound_get_samples(uref, &samples);
        *samples_p = samples;
    }
    return uref->ubuf->planes[0].buffer + offset * uref->ubuf->planes[0].stride;
}

#define UREF_SOUND_BUFFER_TEMPLATE(name, ctype)                             \
/** @This returns a pointer to the buffer space, with ctype type.           \
 *                                                                          \
 * @param uref struct uref structure                                        \
 * @param samples_p reference written with the number of samples per channel\
 * @return pointer to buffer space or NULL in case of error                 \
 */                                                                         \
static inline ctype *uref_sound_buffer_##name(struct uref *uref,            \
                                              size_t *samples_p)            \
{                                                                           \
    return (ctype *)uref_sound_buffer(uref, samples_p);                     \
}
UREF_SOUND_BUFFER_TEMPLATE(u8, uint8_t)
UREF_SOUND_BUFFER_TEMPLATE(s16, int16_t)
UREF_SOUND_BUFFER_TEMPLATE(s32, int32_t)
UREF_SOUND_BUFFER_TEMPLATE(f32, float)
UREF_SOUND_BUFFER_TEMPLATE(f64, double)

/** @This resizes the buffer space pointed to by a uref, in an efficient
 * manner.
 *
 * @param uref_p reference to a uref structure
 * @param ubuf_mgr ubuf management structure in case duplication is needed
 * (may be NULL if the resize is only a space reduction)
 * @param new_samples finale number of samples per channel (if set to -1, keep
 * same end)
 * @param skip number of samples to skip at the beginning of each channel
 * (if < 0, extend the sound backwards)
 * @return true if the operation succeeded
 */
static inline bool uref_sound_resize(struct uref **uref_p,
                                     struct ubuf_mgr *ubuf_mgr,
                                     int new_samples, int skip)
{
    struct uref *uref = *uref_p;
    assert(uref->ubuf != NULL);
    bool ret;
    uint64_t offset = 0;
    uint64_t samples = 0, max_samples;
    uref_sound_get_offset(uref, &offset);
    uref_sound_get_samples(uref, &samples);
    max_samples = samples + offset;
    if (unlikely(new_samples == -1))
        new_samples = samples - skip;

    if (unlikely(skip < 0 && new_samples < -skip)) return false;
    if (unlikely(skip >= 0 && samples < skip)) return false;

    /* if the buffer is not shared, the manager implementation is faster */
    if (likely(ubuf_single(uref->ubuf)))
        goto uref_sound_resize_ubuf;

    /* try just changing the attributes */
    if (likely((int64_t)offset + skip >= 0 &&
               (int64_t)offset + skip + new_samples <= max_samples)) {
        if (likely(skip != 0))
            if (unlikely(!uref_sound_set_offset(uref_p, offset + skip)))
                return false;
        if (likely(new_samples != samples))
            if (unlikely(!uref_sound_set_samples(uref_p, new_samples)))
                return false;
        return true;
    }

    /* we'll have to change the ubuf */
    assert(ubuf_mgr != NULL);

uref_sound_resize_ubuf:
    ret = ubuf_sound_resize(ubuf_mgr, &uref->ubuf, new_samples, offset + skip);
    if (likely(ret)) {
        if (unlikely(!uref_sound_set_samples(uref_p, new_samples)))
            return false;
        uref_sound_delete_offset(*uref_p);
    }
    return ret;
}

#define uref_sound_release uref_release
#define uref_sound_writable uref_ubuf_writable
#define uref_sound_dup uref_dup

#endif

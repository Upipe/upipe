/*****************************************************************************
 * ubuf_sound.c: struct ubuf manager for sound formats
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

/*
 * NB: Not all sound managers are compatible in the manner requested by
 * ubuf_writable() and ubuf_sound_resize() for the new manager. If two different
 * managers are used, they can only differ in the prepend, align and
 * align_offset options, and may obviously not have different channel
 * numbers or sample size options.
 */

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/ulifo.h>
#include <upipe/ubuf_sound.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** default extra samples before buffer when unspecified */
#define UBUF_DEFAULT_PREPEND        8
/** default alignment in octets */
#define UBUF_DEFAULT_ALIGN          16

/** super-set of the ubuf_mgr structure with additional local members */
struct ubuf_sound_mgr {
    /** number of channels */
    uint8_t channels;
    /** sample size */
    uint8_t sample_size;
    /** extra samples added before each channel */
    size_t prepend;
    /** alignment in octets */
    size_t align;
    /** offset for the aligned sample */
    int align_offset;

    /** ubuf pool */
    struct ulifo pool;

    /** common management structure */
    struct ubuf_mgr mgr;
};

/** super-set of the ubuf structure with additional local members */
struct ubuf_sound {
    /** extra space allocated at the end of the structure */
    size_t extra_space;
    /** current number of samples */
    size_t samples;

    /** common structure */
    struct ubuf ubuf;
};

/** @internal @This returns the high-level ubuf structure.
 *
 * @param sound pointer to the ubuf_sound structure
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *ubuf_sound_to_ubuf(struct ubuf_sound *sound)
{
    return &sound->ubuf;
}

/** @internal @This returns the private ubuf_sound structure.
 *
 * @param mgr description structure of the ubuf mgr
 * @return pointer to the ubuf_sound structure
 */
static inline struct ubuf_sound *ubuf_sound_from_ubuf(struct ubuf *ubuf)
{
    return container_of(ubuf, struct ubuf_sound, ubuf);
}

/** @internal @This returns the high-level ubuf_mgr structure.
 *
 * @param sound_mgr pointer to the ubuf_sound_mgr structure
 * @return pointer to the ubuf_mgr structure
 */
static inline struct ubuf_mgr *ubuf_sound_mgr_to_ubuf_mgr(struct ubuf_sound_mgr *sound_mgr)
{
    return &sound_mgr->mgr;
}

/** @internal @This returns the private ubuf_sound_mgr structure.
 *
 * @param mgr description structure of the ubuf mgr
 * @return pointer to the ubuf_sound_mgr structure
 */
static inline struct ubuf_sound_mgr *ubuf_sound_mgr_from_ubuf_mgr(struct ubuf_mgr *mgr)
{
    return container_of(mgr, struct ubuf_sound_mgr, mgr);
}

/** @internal @This allocates the data structure or fetches it from the pool.
 *
 * @param mgr common management structure
 * @param samples number of samples requested
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_sound_alloc_inner(struct ubuf_mgr *mgr, size_t samples)
{
    struct ubuf_sound_mgr *sound_mgr = ubuf_sound_mgr_from_ubuf_mgr(mgr);
    size_t extra_space = (samples + sound_mgr->prepend) *
                             sound_mgr->channels * sound_mgr->sample_size +
                         sound_mgr->align;
    struct ubuf_sound *sound = NULL;
    struct uchain *uchain = ulifo_pop(&sound_mgr->pool, struct uchain *);
    if (likely(uchain != NULL))
        sound = ubuf_sound_from_ubuf(ubuf_from_uchain(uchain));

    if (unlikely(sound == NULL)) {
        sound = malloc(sizeof(struct ubuf_sound) + sizeof(struct ubuf_plane) +
                       extra_space);
        if (unlikely(sound == NULL)) return NULL;
        sound->extra_space = extra_space;
        sound->ubuf.mgr = mgr;
    } else if (unlikely(sound->extra_space < extra_space)) {
        struct ubuf_sound *old_sound = sound;
        sound = realloc(sound, sizeof(struct ubuf_sound) +
                               sizeof(struct ubuf_plane) + extra_space);
        if (unlikely(sound == NULL)) {
            free(old_sound);
            return NULL;
        }
        sound->extra_space = extra_space;
    }

    sound->samples = samples;
    struct ubuf *ubuf = ubuf_sound_to_ubuf(sound);
    ubuf->planes[0].stride = sound_mgr->channels * sound_mgr->sample_size;
    ubuf->planes[0].buffer = (uint8_t *)sound + sizeof(struct ubuf_sound) +
                             sizeof(struct ubuf_plane) +
                             sound_mgr->prepend * ubuf->planes[0].stride +
                             sound_mgr->align;
    ubuf->planes[0].buffer -= ((uintptr_t)ubuf->planes[0].buffer +
                               sound_mgr->align_offset * ubuf->planes[0].stride)
                                   % sound_mgr->align;
    urefcount_init(&ubuf->refcount);
    ubuf_mgr_use(mgr);

    uchain_init(&ubuf->uchain);
    return ubuf;
}

/** @This allocates a ubuf and a sound buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_TYPE_SOUND (sentinel)
 * @param args optional arguments (1st = horizontal size, 2nd = vertical size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *_ubuf_sound_alloc(struct ubuf_mgr *mgr,
                                      enum ubuf_alloc_type alloc_type,
                                      va_list args)
{
    assert(alloc_type == UBUF_ALLOC_TYPE_SOUND);
    int samples;

    /* Parse arguments */
    samples = va_arg(args, int);

    return ubuf_sound_alloc_inner(mgr, samples);
}

/** @This duplicates a ubuf and its sound buffer.
 *
 * @param ubuf ubuf structure to duplicate
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *_ubuf_sound_dup(struct ubuf_mgr *mgr, struct ubuf *ubuf)
{
    struct ubuf_sound *sound = ubuf_sound_from_ubuf(ubuf);
    struct ubuf *new_ubuf = ubuf_sound_alloc_inner(mgr, sound->samples);
    if (unlikely(new_ubuf == NULL)) return NULL;
    assert(new_ubuf->planes[0].stride == ubuf->planes[0].stride);

    memcpy(new_ubuf->planes[0].buffer, ubuf->planes[0].buffer,
           sound->samples * ubuf->planes[0].stride);

    return new_ubuf;
}

/** @internal @This frees a ubuf and all associated data structures.
 *
 * @param sound pointer to a ubuf_sound structure to free
 */
static void _ubuf_sound_free_inner(struct ubuf_sound *sound)
{
    urefcount_clean(&sound->ubuf.refcount);
    free(sound);
}

/** @This frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure to free
 */
static void _ubuf_sound_free(struct ubuf *ubuf)
{
    struct ubuf_sound_mgr *sound_mgr = ubuf_sound_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound *sound = ubuf_sound_from_ubuf(ubuf);

    if (likely(ulifo_push(&sound_mgr->pool, &ubuf->uchain)))
        sound = NULL;
    if (unlikely(sound != NULL))
        _ubuf_sound_free_inner(sound);

    ubuf_mgr_release(&sound_mgr->mgr);
}

/** @This resizes or re-allocates a ubuf with less or more space.
 *
 * @param mgr management structure used to create a new buffer, if needed
 * (can be NULL if ubuf_single(ubuf) and requested sound is smaller than
 * the original)
 * @param ubuf_p reference to a pointer to a ubuf to resize
 * @param alloc_type must be UBUF_ALLOC_TYPE_SOUND (sentinel)
 * @param args optional arguments (1st = number of samples after operation,
 * 2nd = number of samples to skip at the beginning, can be negative)
 * @return false in case of allocation error (unchanged uref)
 */
static bool _ubuf_sound_resize(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                               enum ubuf_alloc_type alloc_type, va_list args)
{
    assert(alloc_type == UBUF_ALLOC_TYPE_SOUND);
    struct ubuf *ubuf = *ubuf_p;
    struct ubuf_sound *sound = ubuf_sound_from_ubuf(ubuf);
    int stride = ubuf->planes[0].stride;
    int new_samples = -1;
    int skip;
    int arg;

    /* Parse arguments */
    arg = va_arg(args, int); if (arg >= 0) new_samples = arg;
    arg = va_arg(args, int); skip = arg;
    if (unlikely(new_samples == -1))
        new_samples = sound->samples - skip;

    if (unlikely(skip < 0 && new_samples < -skip)) return false;
    if (unlikely(skip >= 0 && sound->samples < skip)) return false;

    /* if ubuf is in use, allocate a new one with the needed size */
    if (unlikely(!ubuf_single(ubuf))) {
        assert(mgr != NULL);
        struct ubuf *new_ubuf = ubuf_sound_alloc(mgr, new_samples);
        if (unlikely(new_ubuf == NULL)) return false;
        assert(new_ubuf->planes[0].stride == stride);

        if (likely(skip >= 0))
            memcpy(new_ubuf->planes[0].buffer,
                   ubuf->planes[0].buffer + skip * stride,
                   (likely(new_samples <= sound->samples - skip) ?
                       new_samples : sound->samples - skip) * stride);
        else
            memcpy(new_ubuf->planes[0].buffer - skip * ubuf->planes[0].stride,
                   ubuf->planes[0].buffer,
                   (likely(new_samples + skip <= sound->samples) ?
                       new_samples + skip : sound->samples) * stride);

        ubuf_release(ubuf);
        *ubuf_p = new_ubuf;
        return true;
    }

    ptrdiff_t offset = ubuf->planes[0].buffer - (uint8_t *)sound;
    ptrdiff_t lower = sizeof(struct ubuf_sound) + sizeof(struct ubuf_plane);
    ptrdiff_t higher = lower + sound->extra_space;

    /* try just changing the pointers */
    if (likely(offset + skip * stride >= lower &&
               offset + (skip + new_samples) * stride <= higher)) {
        ubuf->planes[0].buffer += skip * stride;
        sound->samples = new_samples;
        return true;
    }

    /* try just extending the buffer with realloc() */
    if (likely(offset + skip * stride >= lower)) {
        int append = new_samples * stride - (higher - offset);
        if (unlikely(skip >= 0)) append += skip * stride;
        sound = realloc(sound, sizeof(struct ubuf_sound) +
                               sizeof(struct ubuf_plane) +
                               sound->extra_space + append);
        if (unlikely(sound == NULL)) return false;
        sound->extra_space += append;
        ubuf = ubuf_sound_to_ubuf(sound);
        ubuf->planes[0].buffer = (uint8_t *)sound + offset + skip * stride;
        sound->samples = new_samples;
        *ubuf_p = ubuf;
        return true;
    }

    /* check if moving the data into the buffer is enough */
    if (unlikely(sound->extra_space < new_samples * stride)) {
        sound = realloc(sound, sizeof(struct ubuf_sound) +
                               sizeof(struct ubuf_plane) +
                               new_samples * stride);
        if (unlikely(sound == NULL)) return false;
        sound->extra_space = new_samples * stride;
        ubuf = ubuf_sound_to_ubuf(sound);
        *ubuf_p = ubuf;
    }

    ubuf->planes[0].buffer = (uint8_t *)sound + sizeof(struct ubuf_sound) +
                             sizeof(struct ubuf_plane);
    if (likely(skip < 0))
        memmove(ubuf->planes[0].buffer - skip * stride,
                (uint8_t *)sound + offset,
                (likely(new_samples + skip <= sound->samples) ?
                    new_samples + skip : sound->samples) * stride);
    else
        memmove(ubuf->planes[0].buffer,
                (uint8_t *)sound + offset + skip * stride,
                (likely(new_samples <= sound->samples - skip) ?
                    new_samples : sound->samples - skip) * stride);
    sound->samples = new_samples;
    return true;
}

/** @This frees a ubuf_mgr structure.
 *
 * @param mgr pointer to a ubuf_mgr structure to free
 */
static void _ubuf_sound_mgr_free(struct ubuf_mgr *mgr)
{
    struct ubuf_sound_mgr *sound_mgr = ubuf_sound_mgr_from_ubuf_mgr(mgr);
    struct uchain *uchain;

    while ((uchain = ulifo_pop(&sound_mgr->pool, struct uchain *)) != NULL) {
        struct ubuf_sound *sound = ubuf_sound_from_ubuf(ubuf_from_uchain(uchain));
        _ubuf_sound_free_inner(sound);
    }
    ulifo_clean(&sound_mgr->pool);

    urefcount_clean(&sound_mgr->mgr.refcount);
    free(sound_mgr);
}

/** @This allocates a new instance of the ubuf manager for sound formats.
 *
 * @param pool_depth maximum number of sounds in the pool
 * @param channels number of channels
 * @param sample_size size in octets of a sample of an audio channel
 * @param prepend extra samples added before each channel (if set to -1, a
 * default sensible value is used)
 * @param align alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_offset offset of the aligned sample (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_sound_mgr_alloc(unsigned int pool_depth, uint8_t channels,
                                      uint8_t sample_size, int prepend,
                                      int align, int align_offset)
{
    if (unlikely(!channels || !sample_size)) return NULL;
    struct ubuf_sound_mgr *sound_mgr = malloc(sizeof(struct ubuf_sound_mgr) +
                                              ulifo_sizeof(pool_depth));
    if (unlikely(sound_mgr == NULL)) return NULL;

    sound_mgr->channels = channels;
    sound_mgr->sample_size = sample_size;
    sound_mgr->prepend = prepend >= 0 ? prepend : UBUF_DEFAULT_PREPEND;
    sound_mgr->align = align > 0 ? align : UBUF_DEFAULT_ALIGN;
    sound_mgr->align_offset = align_offset;

    ulifo_init(&sound_mgr->pool, pool_depth,
               (void *)sound_mgr + sizeof(struct ubuf_sound_mgr));

    urefcount_init(&sound_mgr->mgr.refcount);
    sound_mgr->mgr.ubuf_alloc = _ubuf_sound_alloc;
    sound_mgr->mgr.ubuf_dup = _ubuf_sound_dup;
    sound_mgr->mgr.ubuf_free = _ubuf_sound_free;
    sound_mgr->mgr.ubuf_resize = _ubuf_sound_resize;
    sound_mgr->mgr.ubuf_mgr_free = _ubuf_sound_mgr_free;

    return ubuf_sound_mgr_to_ubuf_mgr(sound_mgr);
}

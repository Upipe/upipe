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

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound_common.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

/** @This duplicates the content of the common structure for sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @return an error code
 */
int ubuf_sound_common_dup(struct ubuf *ubuf, struct ubuf *new_ubuf)
{
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);
    struct ubuf_sound_common *new_common =
        ubuf_sound_common_from_ubuf(new_ubuf);
    new_common->size = common->size;
    return UBASE_ERR_NONE;
}

/** @This duplicates the content of the plane sub-structure for sound ubuf.
 * It is only necessary to call this function if you plan to use
 * @ref ubuf_sound_common_plane_map.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @param plane index of the plane
 * @return an error code
 */
int ubuf_sound_common_plane_dup(struct ubuf *ubuf, struct ubuf *new_ubuf,
                                uint8_t plane)
{
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);
    struct ubuf_sound_common *new_common =
        ubuf_sound_common_from_ubuf(new_ubuf);
    new_common->planes[plane].buffer = common->planes[plane].buffer;
    return UBASE_ERR_NONE;
}

/** @This returns the sizes of the sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param size_p reference written with the number of samples of the sound
 * if not NULL
 * @param sample_size_p reference written with the number of octets in a
 * sample of a plane if not NULL
 * @return an error code
 */
int ubuf_sound_common_size(struct ubuf *ubuf, size_t *size_p,
                           uint8_t *sample_size_p)
{
    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);
    if (likely(size_p != NULL))
        *size_p = common->size;
    if (likely(sample_size_p != NULL))
        *sample_size_p = common_mgr->sample_size;
    return UBASE_ERR_NONE;
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
int ubuf_sound_common_plane_iterate(struct ubuf *ubuf, const char **channel_p)
{
    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(ubuf->mgr);
    int plane;
    if (*channel_p != NULL) {
        plane = ubuf_sound_common_plane(ubuf->mgr, *channel_p);
        if (unlikely(plane < 0))
            return UBASE_ERR_INVALID;
        plane++;
    } else
        plane = 0;

    if (plane < common_mgr->nb_planes)
        *channel_p = common_mgr->planes[plane]->channel;
    else
        *channel_p = NULL;
    return UBASE_ERR_NONE;
}

/** @This returns a pointer to the buffer space of a plane.
 *
 * To use this function, @ref ubuf_sound_common_plane must be allocated and
 * correctly filled in.
 *
 * @param ubuf pointer to ubuf
 * @param channel channel type (see channel reference)
 * @param offset offset of the sound area wanted in the whole
 * sound, negative values start from the end of lines, in samples
 * @param size number of samples wanted, or -1 for until the end of the buffer
 * @param buffer_p reference written with a pointer to buffer space if not NULL
 * @return an error code
 */
int ubuf_sound_common_plane_map(struct ubuf *ubuf, const char *channel,
                                int offset, int size, uint8_t **buffer_p)
{
    assert(channel != NULL);
    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);
    int plane = ubuf_sound_common_plane(ubuf->mgr, channel);
    if (unlikely(plane < 0))
        return UBASE_ERR_INVALID;

    /* Check offsets. */
    if (offset < 0)
        offset = common->size + offset;

    /* Check sizes - we don't actually use them. */
    if (size < 0)
        size = common->size - offset;
    else if (unlikely(size > common->size - offset))
        return UBASE_ERR_INVALID;

    if (likely(buffer_p != NULL))
        *buffer_p = common->planes[plane].buffer +
                    offset * common_mgr->sample_size;
    return UBASE_ERR_NONE;
}

/** @This shrinks a sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * samples, negative values start from the end
 * @param new_size final size of the buffer, in samples (if set
 * to -1, keep same end)
 * @return an error code
 */
int ubuf_sound_common_resize(struct ubuf *ubuf, int offset, int new_size)
{
    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);

    if (offset < 0)
        offset += common->size;
    if (unlikely(offset < 0))
        return UBASE_ERR_INVALID;
    if (unlikely(new_size == -1))
        new_size = common->size - offset;
    if (unlikely(!offset && new_size == common->size))
        return UBASE_ERR_NONE; /* nothing to do */
    if (unlikely(offset + new_size > common->size))
        return UBASE_ERR_INVALID;

    for (uint8_t plane = 0; plane < common_mgr->nb_planes; plane++)
        common->planes[plane].buffer += offset * common_mgr->sample_size;

    common->size = new_size;
    return UBASE_ERR_NONE;
}

/** @This frees memory allocated by @ref ubuf_sound_common_mgr_init and
 * @ref ubuf_sound_common_mgr_add_plane.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a
 * ubuf_sound_common_mgr
 */
void ubuf_sound_common_mgr_clean(struct ubuf_mgr *mgr)
{
    struct ubuf_sound_common_mgr *sound_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(mgr);

    for (uint8_t plane = 0; plane < sound_mgr->nb_planes; plane++) {
        free(sound_mgr->planes[plane]->channel);
        free(sound_mgr->planes[plane]);
    }
    free(sound_mgr->planes);
}

/** @This allocates a new instance of the ubuf manager for sound formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a
 * ubuf_sound_common_mgr
 * @param sample_size size in octets of a sample
 */
void ubuf_sound_common_mgr_init(struct ubuf_mgr *mgr, uint8_t sample_size)
{
    assert(sample_size);

    struct ubuf_sound_common_mgr *sound_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(mgr);

    sound_mgr->sample_size = sample_size;
    sound_mgr->nb_planes = 0;
    sound_mgr->planes = NULL;
}

/** @This adds a new plane to a ubuf manager for sound formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a
 * ubuf_sound_common_mgr
 * @param channel channel type (see channel reference)
 * @return an error code
 */
int ubuf_sound_common_mgr_add_plane(struct ubuf_mgr *mgr, const char *channel)
{
    assert(channel != NULL);
    assert(mgr->refcount == NULL || urefcount_single(mgr->refcount));

    struct ubuf_sound_common_mgr *sound_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(mgr);
    struct ubuf_sound_common_mgr_plane *plane =
        malloc(sizeof(struct ubuf_sound_common_mgr_plane));
    if (unlikely(plane == NULL))
        return UBASE_ERR_INVALID;

    plane->channel = strdup(channel);

    struct ubuf_sound_common_mgr_plane **planes = realloc(sound_mgr->planes,
        (sound_mgr->nb_planes + 1) *
        sizeof(struct ubuf_sound_common_mgr_plane *));
    if (unlikely(planes == NULL)) {
        free(plane);
        return UBASE_ERR_ALLOC;
    }
    sound_mgr->planes = planes;
    sound_mgr->planes[sound_mgr->nb_planes++] = plane;
    return UBASE_ERR_NONE;
}

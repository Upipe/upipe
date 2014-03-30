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
 * @short Upipe useful common definitions for sound managers
 */

#ifndef _UPIPE_UBUF_SOUND_COMMON_H_
/** @hidden */
#define _UPIPE_UBUF_SOUND_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>

/** @This is a sub-structure of @ref ubuf_sound_common, pointing to the buffer
 * space of individual planes. It is only needed to allocate and init it
 * if you plan to use @ref ubuf_sound_common_plane_map. */
struct ubuf_sound_common_plane {
    /** pointer to buffer space */
    uint8_t *buffer;
};

/** @This is a proposed common section of sound ubuf, allowing to window
 * data. In an opaque area you would typically store a pointer to shared
 * buffer space.
 *
 * Since it features a flexible array member, it must be placed at the end of
 * another structure. */
struct ubuf_sound_common {
    /** requested number of samples */
    size_t size;

    /** common structure */
    struct ubuf ubuf;

    /** planes buffers */
    struct ubuf_sound_common_plane planes[];
};

/** @This is a sub-structure of @ref ubuf_sound_common_mgr, describing the
 * allocation of individual planes. */
struct ubuf_sound_common_mgr_plane {
    /** channel type */
    char *channel;
};

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members, common to sound managers. */
struct ubuf_sound_common_mgr {
    /** number of octets per plane in a sample */
    uint8_t sample_size;
    /** number of planes to allocate */
    uint8_t nb_planes;
    /** planes description */
    struct ubuf_sound_common_mgr_plane **planes;

    /** common management structure */
    struct ubuf_mgr mgr;
};

UBASE_FROM_TO(ubuf_sound_common, ubuf, ubuf, ubuf)
UBASE_FROM_TO(ubuf_sound_common_mgr, ubuf_mgr, ubuf_mgr, mgr)

/** @internal @This returns the plane number corresponding to a channel.
 *
 * @param mgr common management structure
 * @param channel channel type
 * @return number of the plane, or -1 if not found
 */
static inline int ubuf_sound_common_plane(struct ubuf_mgr *mgr,
                                          const char *channel)
{
    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(mgr);
    for (int i = 0; i < common_mgr->nb_planes; i++)
        if (!strcmp(common_mgr->planes[i]->channel, channel))
            return i;
    return -1;
}

/** @This returns the number of extra octets needed when allocating a sound
 * ubuf. It is only necessary to allocate them if you plan to use
 * @ref ubuf_sound_common_plane_map.
 *
 * @param mgr description structure of the ubuf manager
 * @return number of extra octets needed
 */
static inline size_t ubuf_sound_common_sizeof(struct ubuf_mgr *mgr)
{
    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(mgr);
    return sizeof(struct ubuf_sound_common_plane) * common_mgr->nb_planes;
}

/** @This initializes the common fields of a sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param size number of samples
 */
static inline void ubuf_sound_common_init(struct ubuf *ubuf, size_t size)
{
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);
    common->size = size;
    uchain_init(&ubuf->uchain);
}

/** @This cleans up the common fields of a sound ubuf (currently no-op).
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_sound_common_clean(struct ubuf *ubuf)
{
}

/** @This initializes a plane sub-structure of a sound ubuf. It is only
 * necessary to call this function if you plan to use
 * @ref ubuf_sound_common_plane_map.
 *
 * @param ubuf pointer to ubuf
 * @param plane index of the plane
 * @param buffer pointer to memory buffer
 */
static inline void ubuf_sound_common_plane_init(struct ubuf *ubuf,
                                                uint8_t plane, uint8_t *buffer)
{
    struct ubuf_sound_common *common = ubuf_sound_common_from_ubuf(ubuf);
    common->planes[plane].buffer = buffer;
}

/** @This cleans up a plane sub-structure of a sound ubuf (currently no-op).
 *
 * @param ubuf pointer to ubuf
 * @param plane index of the plane
 */
static inline void ubuf_sound_common_plane_clean(struct ubuf *ubuf,
                                                 uint8_t plane)
{
}

/** @This duplicates the content of the common structure for sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @return an error code
 */
int ubuf_sound_common_dup(struct ubuf *ubuf, struct ubuf *new_ubuf);

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
                                uint8_t plane);

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
                           uint8_t *sample_size_p);

/** @This iterates on sound planes channel types. Start by initializing
 * *channel_p to NULL. If *channel_p is NULL after running this function, there
 * are no more planes in this sound. Otherwise the string pointed to by
 * *channel_p remains valid until the ubuf sound manager is deallocated.
 *
 * @param ubuf pointer to ubuf
 * @param channel_p reference written with channel type of the next plane
 * @return an error code
 */
int ubuf_sound_common_plane_iterate(struct ubuf *ubuf, const char **channel_p);

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
                                int offset, int size, uint8_t **buffer_p);

/** @This shrinks a sound ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param offset offset of the buffer space wanted in the whole block, in
 * samples, negative values start from the end
 * @param new_size final size of the buffer, in samples (if set
 * to -1, keep same end)
 * @return an error code
 */
int ubuf_sound_common_resize(struct ubuf *ubuf, int offset, int new_size);

/** @This frees memory allocated by @ref ubuf_sound_common_mgr_init and
 * @ref ubuf_sound_common_mgr_add_plane.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a
 * ubuf_sound_common_mgr
 */
void ubuf_sound_common_mgr_clean(struct ubuf_mgr *mgr);

/** @This allocates a new instance of the ubuf manager for sound formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a
 * ubuf_sound_common_mgr
 * @param sample_size size in octets of a sample
 */
void ubuf_sound_common_mgr_init(struct ubuf_mgr *mgr, uint8_t sample_size);

/** @This adds a new plane to a ubuf manager for sound formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a
 * ubuf_sound_common_mgr
 * @param channel channel type (see channel reference)
 * @return an error code
 */
int ubuf_sound_common_mgr_add_plane(struct ubuf_mgr *mgr, const char *channel);

#ifdef __cplusplus
}
#endif
#endif

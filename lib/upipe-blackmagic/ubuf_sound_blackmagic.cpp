/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe ubuf manager for sound formats with blackmagic storage
 */

#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/urefcount.h>
#include <upipe/upool.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_common.h>
#include <upipe-blackmagic/ubuf_sound_blackmagic.h>
#include <upipe/ubuf_mem_common.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "include/DeckLinkAPI.h"

/** @This is a super-set of the @ref ubuf (and @ref ubuf_sound_common)
 * structure with private fields pointing to shared data. */
struct ubuf_sound_bmd {
    /** pointer to shared structure */
    IDeckLinkAudioInputPacket *shared;

    /** common sound structure */
    struct ubuf_sound_common ubuf_sound_common;
};

UBASE_FROM_TO(ubuf_sound_bmd, ubuf, ubuf, ubuf_sound_common.ubuf)

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members. */
struct ubuf_sound_bmd_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf pool */
    struct upool ubuf_pool;

    /** common sound management structure */
    struct ubuf_sound_common_mgr common_mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(ubuf_sound_bmd_mgr, ubuf_mgr, ubuf_mgr, common_mgr.mgr)
UBASE_FROM_TO(ubuf_sound_bmd_mgr, urefcount, urefcount, urefcount)
UBASE_FROM_TO(ubuf_sound_bmd_mgr, upool, ubuf_pool, ubuf_pool)

/** @This allocates a ubuf, a shared structure and a umem buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_SOUND (sentinel)
 * @param args optional arguments (1st = size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_sound_bmd_alloc(struct ubuf_mgr *mgr,
                                         uint32_t signature, va_list args)
{
    if (unlikely(signature != UBUF_BMD_ALLOC_SOUND))
        return NULL;

    struct ubuf_sound_bmd_mgr *sound_mgr =
        ubuf_sound_bmd_mgr_from_ubuf_mgr(mgr);
    void *_AudioFrame = va_arg(args, void *);
    IDeckLinkAudioInputPacket *AudioFrame =
        (IDeckLinkAudioInputPacket *)_AudioFrame;

    struct ubuf_sound_bmd *sound_bmd = upool_alloc(&sound_mgr->ubuf_pool,
                                                   struct ubuf_sound_bmd *);
    if (unlikely(sound_bmd == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_sound_bmd_to_ubuf(sound_bmd);

    sound_bmd->shared = AudioFrame;
    AudioFrame->AddRef();
    ubuf_sound_common_init(ubuf, AudioFrame->GetSampleFrameCount());

    void *buffer;
    AudioFrame->GetBytes(&buffer);
    ubuf_sound_common_plane_init(ubuf, 0, (uint8_t *)buffer);

    return ubuf;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @return an error code
 */
static int ubuf_sound_bmd_dup(struct ubuf *ubuf, struct ubuf **new_ubuf_p)
{
    assert(new_ubuf_p != NULL);
    struct ubuf_sound_bmd_mgr *sound_mgr =
        ubuf_sound_bmd_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound_bmd *new_sound = upool_alloc(&sound_mgr->ubuf_pool,
                                                   struct ubuf_sound_bmd *);
    if (unlikely(new_sound == NULL))
        return UBASE_ERR_ALLOC;

    struct ubuf *new_ubuf = ubuf_sound_bmd_to_ubuf(new_sound);
    if (unlikely(!ubase_check(ubuf_sound_common_dup(ubuf, new_ubuf)))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    for (uint8_t plane = 0; plane < sound_mgr->common_mgr.nb_planes; plane++) {
        if (unlikely(!ubase_check(ubuf_sound_common_plane_dup(ubuf, new_ubuf, plane)))) {
            ubuf_free(new_ubuf);
            return UBASE_ERR_INVALID;
        }
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_sound_bmd *sound_bmd = ubuf_sound_bmd_from_ubuf(ubuf);
    new_sound->shared = sound_bmd->shared;
    sound_bmd->shared->AddRef();
    return UBASE_ERR_NONE;
}

/** @This handles control commands.
 *
 * @param ubuf pointer to ubuf
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_sound_bmd_control(struct ubuf *ubuf, int command, va_list args)
{
    switch (command) {
        case UBUF_DUP: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            return ubuf_sound_bmd_dup(ubuf, new_ubuf_p);
        }
        case UBUF_SIZE_SOUND: {
            size_t *size_p = va_arg(args, size_t *);
            uint8_t *sample_size_p = va_arg(args, uint8_t *);
            return ubuf_sound_common_size(ubuf, size_p, sample_size_p);
        }
        case UBUF_ITERATE_SOUND_PLANE: {
            const char **chroma_p = va_arg(args, const char **);
            return ubuf_sound_common_iterate_plane(ubuf, chroma_p);
        }
        case UBUF_READ_SOUND_PLANE: {
            const char *chroma = va_arg(args, const char *);
            int offset = va_arg(args, int);
            int size = va_arg(args, int);
            uint8_t **buffer_p = va_arg(args, uint8_t **);
            return ubuf_sound_common_plane_map(ubuf, chroma, offset,
                                               size, buffer_p);
        }
        case UBUF_WRITE_SOUND_PLANE: {
            /* There is no way to know reference count */
            return UBASE_ERR_BUSY;
        }
        case UBUF_UNMAP_SOUND_PLANE: {
            /* we don't actually care about the parameters */
            return UBASE_ERR_NONE;
        }
        case UBUF_RESIZE_SOUND: {
            int offset = va_arg(args, int);
            int new_size = va_arg(args, int);
            return ubuf_sound_common_resize(ubuf, offset, new_size);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This recycles or frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure
 */
static void ubuf_sound_bmd_free(struct ubuf *ubuf)
{
    struct ubuf_mgr *mgr = ubuf->mgr;
    struct ubuf_sound_bmd_mgr *sound_mgr =
        ubuf_sound_bmd_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound_bmd *sound_bmd = ubuf_sound_bmd_from_ubuf(ubuf);

    ubuf_sound_common_clean(ubuf);
    for (uint8_t plane = 0; plane < sound_mgr->common_mgr.nb_planes; plane++)
        ubuf_sound_common_plane_clean(ubuf, plane);

    sound_bmd->shared->Release();
    upool_free(&sound_mgr->ubuf_pool, sound_bmd);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to ubuf_sound_bmd or NULL in case of allocation error
 */
static void *ubuf_sound_bmd_alloc_inner(struct upool *upool)
{
    struct ubuf_sound_bmd_mgr *sound_bmd_mgr =
        ubuf_sound_bmd_mgr_from_ubuf_pool(upool);
    struct ubuf_mgr *mgr = ubuf_sound_bmd_mgr_to_ubuf_mgr(sound_bmd_mgr);
    struct ubuf_sound_bmd *sound_bmd =
        (struct ubuf_sound_bmd *)malloc(sizeof(struct ubuf_sound_bmd) +
                                        ubuf_sound_common_sizeof(mgr));
    if (unlikely(sound_bmd == NULL))
        return NULL;
    struct ubuf *ubuf = ubuf_sound_bmd_to_ubuf(sound_bmd);
    ubuf->mgr = mgr;
    return sound_bmd;
}

/** @internal @This frees a ubuf_sound_bmd.
 *
 * @param upool pointer to upool
 * @param _sound_bmd pointer to a ubuf_sound_bmd structure to free
 */
static void ubuf_sound_bmd_free_inner(struct upool *upool, void *_sound_bmd)
{
    struct ubuf_sound_bmd *sound_bmd = (struct ubuf_sound_bmd *)_sound_bmd;
    free(sound_bmd);
}

/** @This handles manager control commands.
 *
 * @param mgr pointer to ubuf manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_sound_bmd_mgr_control(struct ubuf_mgr *mgr,
                                      int command, va_list args)
{
    switch (command) {
        case UBUF_MGR_VACUUM: {
            struct ubuf_sound_bmd_mgr *sound_mgr =
                ubuf_sound_bmd_mgr_from_ubuf_mgr(mgr);
            upool_clean(&sound_mgr->ubuf_pool);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a ubuf manager.
 *
 * @param urefcount pointer to urefcount
 */
static void ubuf_sound_bmd_mgr_free(struct urefcount *urefcount)
{
    struct ubuf_sound_bmd_mgr *sound_mgr =
        ubuf_sound_bmd_mgr_from_urefcount(urefcount);
    struct ubuf_mgr *mgr = ubuf_sound_bmd_mgr_to_ubuf_mgr(sound_mgr);
    upool_clean(&sound_mgr->ubuf_pool);

    ubuf_sound_common_mgr_clean(mgr);

    urefcount_clean(urefcount);
    free(sound_mgr);
}

/** @This allocates a new instance of the ubuf manager for sound formats
 * using blackmagic.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param SampleType blackmagic sample type
 * @param nb_channels number of channels
 * @param channel channel type (see channel reference)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_sound_bmd_mgr_alloc(uint16_t ubuf_pool_depth,
                                          uint32_t SampleType,
                                          uint8_t nb_channels,
                                          const char *channel)
{
    switch (SampleType) {
        case bmdAudioSampleType16bitInteger:
        case bmdAudioSampleType32bitInteger:
            break;
        default:
            return NULL;
    }

    struct ubuf_sound_bmd_mgr *sound_mgr =
        (struct ubuf_sound_bmd_mgr *)malloc(sizeof(struct ubuf_sound_bmd_mgr) +
                                            upool_sizeof(ubuf_pool_depth));
    if (unlikely(sound_mgr == NULL))
        return NULL;

    struct ubuf_mgr *mgr = ubuf_sound_bmd_mgr_to_ubuf_mgr(sound_mgr);
    ubuf_sound_common_mgr_init(mgr, nb_channels *
            (SampleType == bmdAudioSampleType16bitInteger ? 2 : 4));

    urefcount_init(ubuf_sound_bmd_mgr_to_urefcount(sound_mgr),
                   ubuf_sound_bmd_mgr_free);
    sound_mgr->common_mgr.mgr.refcount = ubuf_sound_bmd_mgr_to_urefcount(sound_mgr);

    mgr->signature = UBUF_BMD_ALLOC_SOUND;
    mgr->ubuf_alloc = ubuf_sound_bmd_alloc;
    mgr->ubuf_control = ubuf_sound_bmd_control;
    mgr->ubuf_free = ubuf_sound_bmd_free;
    mgr->ubuf_mgr_control = ubuf_sound_bmd_mgr_control;

    upool_init(&sound_mgr->ubuf_pool, mgr->refcount, ubuf_pool_depth,
               sound_mgr->upool_extra,
               ubuf_sound_bmd_alloc_inner, ubuf_sound_bmd_free_inner);

    if (unlikely(!ubase_check(ubuf_sound_common_mgr_add_plane(mgr, channel)))) {
        ubuf_mgr_release(mgr);
        return NULL;
    }

    return mgr;
}

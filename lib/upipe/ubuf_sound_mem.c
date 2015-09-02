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
 * @short Upipe ubuf manager for sound formats with umem storage
 */

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/urefcount.h>
#include <upipe/upool.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_common.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/ubuf_mem_common.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_sound_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** default alignment in octets */
#define UBUF_DEFAULT_ALIGN          0

/** @This is a super-set of the @ref ubuf (and @ref ubuf_sound_common)
 * structure with private fields pointing to shared data. */
struct ubuf_sound_mem {
    /** pointer to shared structure */
    struct ubuf_mem_shared *shared;
#ifndef NDEBUG
    /** atomic counter of the number of readers, to check for unsufficient
     * use of unmap() */
    uatomic_uint32_t readers;
#endif

    /** common sound structure */
    struct ubuf_sound_common ubuf_sound_common;
};

UBASE_FROM_TO(ubuf_sound_mem, ubuf, ubuf, ubuf_sound_common.ubuf)

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members. */
struct ubuf_sound_mem_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** alignment in octets */
    size_t align;

    /** ubuf pool */
    struct upool ubuf_pool;
    /** ubuf shared pool */
    struct upool shared_pool;
    /** umem allocator */
    struct umem_mgr *umem_mgr;

    /** common sound management structure */
    struct ubuf_sound_common_mgr common_mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(ubuf_sound_mem_mgr, ubuf_mgr, ubuf_mgr, common_mgr.mgr)
UBASE_FROM_TO(ubuf_sound_mem_mgr, urefcount, urefcount, urefcount)
UBASE_FROM_TO(ubuf_sound_mem_mgr, upool, ubuf_pool, ubuf_pool)

UBUF_MEM_MGR_HELPER_POOL(ubuf_sound_mem, ubuf_pool, shared_pool, shared)

/** @This allocates a ubuf, a shared structure and a umem buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_SOUND (sentinel)
 * @param args optional arguments (1st = size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_sound_mem_alloc(struct ubuf_mgr *mgr,
                                         uint32_t signature, va_list args)
{
    if (unlikely(signature != UBUF_ALLOC_SOUND))
        return NULL;

    int ssize = va_arg(args, int);
    if (ssize < 0)
        return NULL;
    size_t size = ssize;

    struct ubuf_sound_mem_mgr *sound_mgr =
        ubuf_sound_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf_sound_mem *sound_mem = ubuf_sound_mem_alloc_pool(mgr);
    if (unlikely(sound_mem == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_sound_mem_to_ubuf(sound_mem);

    sound_mem->shared = ubuf_sound_mem_shared_alloc_pool(mgr);
    if (unlikely(sound_mem->shared == NULL)) {
        ubuf_sound_mem_free_pool(mgr, sound_mem);
        return NULL;
    }

    size_t buffer_size = 0;
    size_t plane_sizes[sound_mgr->common_mgr.nb_planes];
    for (uint8_t plane = 0; plane < sound_mgr->common_mgr.nb_planes; plane++) {
        size_t align = 0;
        size_t plane_size;
        if (sound_mgr->align && (size * sound_mgr->common_mgr.sample_size) % sound_mgr->align)
            align = sound_mgr->align;
        plane_size = (size * sound_mgr->common_mgr.sample_size) + align;
        if (align)
            plane_size -= plane_size % align;
        plane_sizes[plane] = plane_size + sound_mgr->align;
        buffer_size += plane_sizes[plane];
    }

    if (unlikely(!umem_alloc(sound_mgr->umem_mgr, &sound_mem->shared->umem,
                             buffer_size))) {
        ubuf_sound_mem_shared_free_pool(mgr, sound_mem->shared);
        ubuf_sound_mem_free_pool(mgr, sound_mem);
        return NULL;
    }
    ubuf_sound_common_init(ubuf, size);

    uint8_t *buffer = ubuf_mem_shared_buffer(sound_mem->shared);
    for (uint8_t plane = 0; plane < sound_mgr->common_mgr.nb_planes; plane++) {
        uint8_t *plane_buffer = buffer + sound_mgr->align;
        if (sound_mgr->align)
            plane_buffer -= ((uintptr_t)plane_buffer) % sound_mgr->align;
        ubuf_sound_common_plane_init(ubuf, plane, plane_buffer);
        buffer += plane_sizes[plane];
    }

    ubuf_mgr_use(mgr);
    return ubuf;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @return an error code
 */
static int ubuf_sound_mem_dup(struct ubuf *ubuf, struct ubuf **new_ubuf_p)
{
    assert(new_ubuf_p != NULL);
    struct ubuf_sound_mem *new_sound = ubuf_sound_mem_alloc_pool(ubuf->mgr);
    if (unlikely(new_sound == NULL))
        return UBASE_ERR_ALLOC;

    struct ubuf *new_ubuf = ubuf_sound_mem_to_ubuf(new_sound);
    if (unlikely(!ubase_check(ubuf_sound_common_dup(ubuf, new_ubuf)))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    struct ubuf_sound_mem_mgr *sound_mgr =
        ubuf_sound_mem_mgr_from_ubuf_mgr(ubuf->mgr);
    for (uint8_t plane = 0; plane < sound_mgr->common_mgr.nb_planes; plane++) {
        if (unlikely(!ubase_check(ubuf_sound_common_plane_dup(ubuf, new_ubuf, plane)))) {
            ubuf_free(new_ubuf);
            return UBASE_ERR_INVALID;
        }
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_sound_mem *sound_mem = ubuf_sound_mem_from_ubuf(ubuf);
    new_sound->shared = ubuf_mem_shared_use(sound_mem->shared);
    ubuf_mgr_use(new_ubuf->mgr);
    return UBASE_ERR_NONE;
}

/** @This handles control commands.
 *
 * @param ubuf pointer to ubuf
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_sound_mem_control(struct ubuf *ubuf, int command, va_list args)
{
    switch (command) {
        case UBUF_DUP: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            return ubuf_sound_mem_dup(ubuf, new_ubuf_p);
        }
        case UBUF_SIZE_SOUND: {
            size_t *size_p = va_arg(args, size_t *);
            uint8_t *sample_size_p = va_arg(args, uint8_t *);
            return ubuf_sound_common_size(ubuf, size_p, sample_size_p);
        }
        case UBUF_ITERATE_SOUND_PLANE: {
            const char **chroma_p = va_arg(args, const char **);
            return ubuf_sound_common_plane_iterate(ubuf, chroma_p);
        }
        case UBUF_READ_SOUND_PLANE: {
            const char *chroma = va_arg(args, const char *);
            int offset = va_arg(args, int);
            int size = va_arg(args, int);
            uint8_t **buffer_p = va_arg(args, uint8_t **);
            int err =
                ubuf_sound_common_plane_map(ubuf, chroma, offset,
                                            size, buffer_p);
#ifndef NDEBUG
            if (ubase_check(err)) {
                struct ubuf_sound_mem *sound = ubuf_sound_mem_from_ubuf(ubuf);
                uatomic_fetch_add(&sound->readers, 1);
            }
#endif
            return err;
        }
        case UBUF_WRITE_SOUND_PLANE: {
            const char *chroma = va_arg(args, const char *);
            int offset = va_arg(args, int);
            int size = va_arg(args, int);
            uint8_t **buffer_p = va_arg(args, uint8_t **);
            struct ubuf_sound_mem *sound = ubuf_sound_mem_from_ubuf(ubuf);
            if (!ubuf_mem_shared_single(sound->shared))
                return UBASE_ERR_BUSY;
            int err =
                ubuf_sound_common_plane_map(ubuf, chroma, offset,
                                            size, buffer_p);
#ifndef NDEBUG
            if (ubase_check(err))
                uatomic_fetch_add(&sound->readers, 1);
#endif
            return err;
        }
        case UBUF_UNMAP_SOUND_PLANE: {
            /* we don't actually care about the parameters */
#ifndef NDEBUG
            struct ubuf_sound_mem *sound = ubuf_sound_mem_from_ubuf(ubuf);
            uatomic_fetch_sub(&sound->readers, 1);
#endif
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
static void ubuf_sound_mem_free(struct ubuf *ubuf)
{
    struct ubuf_mgr *mgr = ubuf->mgr;
    struct ubuf_sound_mem_mgr *sound_mgr =
        ubuf_sound_mem_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_sound_mem *sound_mem = ubuf_sound_mem_from_ubuf(ubuf);

    ubuf_sound_common_clean(ubuf);
    for (uint8_t plane = 0; plane < sound_mgr->common_mgr.nb_planes; plane++)
        ubuf_sound_common_plane_clean(ubuf, plane);

#ifndef NDEBUG
    assert(uatomic_load(&sound_mem->readers) == 0);
#endif

    if (unlikely(ubuf_mem_shared_release(sound_mem->shared))) {
        umem_free(&sound_mem->shared->umem);
        ubuf_sound_mem_shared_free_pool(mgr, sound_mem->shared);
    }
    ubuf_sound_mem_free_pool(mgr, sound_mem);
    ubuf_mgr_release(mgr);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to ubuf_sound_mem or NULL in case of allocation error
 */
static void *ubuf_sound_mem_alloc_inner(struct upool *upool)
{
    struct ubuf_sound_mem_mgr *sound_mem_mgr =
        ubuf_sound_mem_mgr_from_ubuf_pool(upool);
    struct ubuf_mgr *mgr = ubuf_sound_mem_mgr_to_ubuf_mgr(sound_mem_mgr);
    struct ubuf_sound_mem *sound_mem = malloc(sizeof(struct ubuf_sound_mem) +
                                          ubuf_sound_common_sizeof(mgr));
    if (unlikely(sound_mem == NULL))
        return NULL;
    struct ubuf *ubuf = ubuf_sound_mem_to_ubuf(sound_mem);
    ubuf->mgr = mgr;
#ifndef NDEBUG
    uatomic_init(&sound_mem->readers, 0);
#endif
    return sound_mem;
}

/** @internal @This frees a ubuf_sound_mem.
 *
 * @param upool pointer to upool
 * @param _sound_mem pointer to a ubuf_sound_mem structure to free
 */
static void ubuf_sound_mem_free_inner(struct upool *upool, void *_sound_mem)
{
    struct ubuf_sound_mem *sound_mem = (struct ubuf_sound_mem *)_sound_mem;
#ifndef NDEBUG
    uatomic_clean(&sound_mem->readers);
#endif
    free(sound_mem);
}

/** @This checks if the given flow format can be allocated with the manager.
 *
 * @param mgr pointer to ubuf manager
 * @param flow_format flow format to check
 * @return an error code
 */
static int ubuf_sound_mem_mgr_check(struct ubuf_mgr *mgr,
                                    struct uref *flow_format)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_format, &def))
    if (ubase_ncmp(def, "sound."))
        return UBASE_ERR_INVALID;

    uint8_t sample_size;
    uint8_t planes;
    uint64_t align = 0;

    UBASE_RETURN(uref_sound_flow_get_sample_size(flow_format, &sample_size))
    UBASE_RETURN(uref_sound_flow_get_planes(flow_format, &planes))
    uref_sound_flow_get_align(flow_format, &align);

    struct ubuf_sound_common_mgr *common_mgr =
        ubuf_sound_common_mgr_from_ubuf_mgr(mgr);
    struct ubuf_sound_mem_mgr *sound_mgr =
        ubuf_sound_mem_mgr_from_ubuf_mgr(mgr);
    if (common_mgr->sample_size != sample_size ||
        common_mgr->nb_planes != planes)
        return UBASE_ERR_INVALID;
    if (align && sound_mgr->align % align)
        return UBASE_ERR_INVALID;

    for (uint8_t i = 0; i < planes; i++) {
        struct ubuf_sound_common_mgr_plane *plane = common_mgr->planes[i];
        const char *channel;
        UBASE_RETURN(uref_sound_flow_get_channel(flow_format, &channel, i))

        if (strcmp(plane->channel, channel))
            return UBASE_ERR_INVALID;
    }
    return UBASE_ERR_NONE;
}

/** @This handles manager control commands.
 *
 * @param mgr pointer to ubuf manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_sound_mem_mgr_control(struct ubuf_mgr *mgr,
                                      int command, va_list args)
{
    switch (command) {
        case UBUF_MGR_CHECK: {
            struct uref *flow_format = va_arg(args, struct uref *);
            return ubuf_sound_mem_mgr_check(mgr, flow_format);
        }
        case UBUF_MGR_VACUUM: {
            ubuf_sound_mem_mgr_vacuum_pool(mgr);
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
static void ubuf_sound_mem_mgr_free(struct urefcount *urefcount)
{
    struct ubuf_sound_mem_mgr *sound_mgr =
        ubuf_sound_mem_mgr_from_urefcount(urefcount);
    struct ubuf_mgr *mgr = ubuf_sound_mem_mgr_to_ubuf_mgr(sound_mgr);
    ubuf_sound_mem_mgr_clean_pool(mgr);
    umem_mgr_release(sound_mgr->umem_mgr);

    ubuf_sound_common_mgr_clean(mgr);

    urefcount_clean(urefcount);
    free(sound_mgr);
}

/** @This allocates a new instance of the ubuf manager for sound formats
 * using umem.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param sample_size number of octets in a sample for a plane
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_sound_mem_mgr_alloc(uint16_t ubuf_pool_depth,
                                          uint16_t shared_pool_depth,
                                          struct umem_mgr *umem_mgr,
                                          uint8_t sample_size,
                                          uint64_t align)
{
    assert(umem_mgr != NULL);

    struct ubuf_sound_mem_mgr *sound_mgr =
        malloc(sizeof(struct ubuf_sound_mem_mgr) +
               ubuf_sound_mem_mgr_sizeof_pool(ubuf_pool_depth,
                                              shared_pool_depth));
    if (unlikely(sound_mgr == NULL))
        return NULL;

    ubuf_sound_mem_mgr_init_pool(ubuf_sound_mem_mgr_to_ubuf_mgr(sound_mgr),
            ubuf_pool_depth, shared_pool_depth, sound_mgr->upool_extra,
            ubuf_sound_mem_alloc_inner, ubuf_sound_mem_free_inner);

    sound_mgr->umem_mgr = umem_mgr;
    sound_mgr->align = align;
    umem_mgr_use(umem_mgr);

    struct ubuf_mgr *mgr = ubuf_sound_mem_mgr_to_ubuf_mgr(sound_mgr);
    ubuf_sound_common_mgr_init(mgr, sample_size);

    urefcount_init(ubuf_sound_mem_mgr_to_urefcount(sound_mgr),
                   ubuf_sound_mem_mgr_free);
    sound_mgr->common_mgr.mgr.refcount = ubuf_sound_mem_mgr_to_urefcount(sound_mgr);

    mgr->signature = UBUF_ALLOC_SOUND;
    mgr->ubuf_alloc = ubuf_sound_mem_alloc;
    mgr->ubuf_control = ubuf_sound_mem_control;
    mgr->ubuf_free = ubuf_sound_mem_free;
    mgr->ubuf_mgr_control = ubuf_sound_mem_mgr_control;

    return mgr;
}

/** @This adds a new plane to a ubuf manager for sound formats using umem.
 * It may only be called on initializing the manager, before any ubuf is
 * allocated.
 *
 * @param mgr pointer to a ubuf_mgr structure
 * @param channel channel type (see channel reference)
 * @return an error code
 */
int ubuf_sound_mem_mgr_add_plane(struct ubuf_mgr *mgr, const char *channel)
{
    assert(mgr != NULL);
    ubuf_sound_mem_mgr_vacuum_pool(mgr);

    return ubuf_sound_common_mgr_add_plane(mgr, channel);
}


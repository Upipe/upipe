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
 * @short Upipe ubuf manager for picture formats with umem storage
 */

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/urefcount.h>
#include <upipe/ulifo.h>
#include <upipe/umem.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_common.h>
#include <upipe/ubuf_pic_mem.h>

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** default extra macropixels before lines when unspecified */
#define UBUF_DEFAULT_HPREPEND       8
/** default extra macropixels after lines when unspecified */
#define UBUF_DEFAULT_HAPPEND        8
/** default extra lines before buffer when unspecified */
#define UBUF_DEFAULT_VPREPEND       2
/** default extra lines after buffer when unspecified */
#define UBUF_DEFAULT_VAPPEND        2
/** default alignment in octets */
#define UBUF_DEFAULT_ALIGN          0

/** @This is the low-level shared structure with reference counting, pointing
 * to the actual data. */
struct ubuf_pic_mem_shared {
    /** refcount management structure */
    uatomic_uint32_t refcount;
    /** umem structure pointing to buffer */
    struct umem umem;
};

/** @This is a super-set of the @ref ubuf (and @ref ubuf_pic_common)
 * structure with private fields pointing to shared data. */
struct ubuf_pic_mem {
    /** pointer to shared structure */
    struct ubuf_pic_mem_shared *shared;
#ifndef NDEBUG
    /** atomic counter of the number of readers, to check for unsufficient
     * use of unmap() */
    uatomic_uint32_t readers;
#endif

    /** common picture structure */
    struct ubuf_pic_common ubuf_pic_common;
};

UBASE_FROM_TO(ubuf_pic_mem, ubuf, ubuf, ubuf_pic_common.ubuf)

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members. */
struct ubuf_pic_mem_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** extra macropixels added before lines */
    size_t hmprepend;
    /** extra macropixels added after lines */
    size_t hmappend;
    /** extra lines added before buffer */
    size_t vprepend;
    /** extra lines added after buffer */
    size_t vappend;
    /** alignment in octets */
    size_t align;
    /** horizontal offset for the aligned macropixel */
    int align_hmoffset;

    /** ubuf pool */
    struct ulifo ubuf_pool;
    /** ubuf shared pool */
    struct ulifo shared_pool;
    /** umem allocator */
    struct umem_mgr *umem_mgr;

    /** common picture management structure */
    struct ubuf_pic_common_mgr common_mgr;
};

UBASE_FROM_TO(ubuf_pic_mem_mgr, ubuf_mgr, ubuf_mgr, common_mgr.mgr)
UBASE_FROM_TO(ubuf_pic_mem_mgr, urefcount, urefcount, urefcount)

/** @hidden */
static void ubuf_pic_mem_free_inner(struct ubuf *ubuf);
/** @hidden */
static void ubuf_pic_mem_shared_free_inner(struct ubuf_pic_mem_shared *shared);

/** @This increments the reference count of a shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_pic_mem_use(struct ubuf *ubuf)
{
    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
    uatomic_fetch_add(&pic->shared->refcount, 1);
}

/** @This checks whether there is only one reference to the shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline bool ubuf_pic_mem_single(struct ubuf *ubuf)
{
    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
    return uatomic_load(&pic->shared->refcount) == 1;
}

/** @This returns the shared buffer.
 *
 * @param ubuf pointer to ubuf
 */
static inline uint8_t *ubuf_pic_mem_buffer(struct ubuf *ubuf)
{
    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
    return umem_buffer(&pic->shared->umem);
}

/** @internal @This allocates the data structure or fetches it from the pool.
 *
 * @param mgr common management structure
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_pic_mem_alloc_inner(struct ubuf_mgr *mgr)
{
    struct ubuf_pic_mem_mgr *pic_mgr = ubuf_pic_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf *ubuf = ulifo_pop(&pic_mgr->ubuf_pool, struct ubuf *);
    struct ubuf_pic_mem *pic;
    if (ubuf == NULL) {
        pic = malloc(sizeof(struct ubuf_pic_mem) + ubuf_pic_common_sizeof(mgr));
        if (unlikely(pic == NULL))
            return NULL;
        ubuf = ubuf_pic_mem_to_ubuf(pic);
        ubuf->mgr = mgr;
#ifndef NDEBUG
        uatomic_init(&pic->readers, 0);
#endif
    } else
        pic = ubuf_pic_mem_from_ubuf(ubuf);

    pic->shared = NULL;
    return ubuf;
}

/** @This allocates a ubuf, a shared structure and a umem buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_PICTURE (sentinel)
 * @param args optional arguments (1st = size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_pic_mem_alloc(struct ubuf_mgr *mgr,
                                       enum ubuf_alloc_type alloc_type,
                                       va_list args)
{
    assert(alloc_type == UBUF_ALLOC_PICTURE);
    int hsize = va_arg(args, int);
    int vsize = va_arg(args, int);

    if (unlikely(!ubase_check(ubuf_pic_common_check_size(mgr, hsize, vsize))))
        return NULL;

    struct ubuf_pic_mem_mgr *pic_mgr =
        ubuf_pic_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf *ubuf = ubuf_pic_mem_alloc_inner(mgr);
    if (unlikely(ubuf == NULL))
        return NULL;

    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
    pic->shared = ulifo_pop(&pic_mgr->shared_pool,
                            struct ubuf_pic_mem_shared *);
    if (pic->shared == NULL) {
        pic->shared = malloc(sizeof(struct ubuf_pic_mem_shared));
        if (unlikely(pic->shared == NULL)) {
            if (unlikely(!ulifo_push(&pic_mgr->ubuf_pool, ubuf)))
                ubuf_pic_mem_free_inner(ubuf);
            return NULL;
        }

        uatomic_init(&pic->shared->refcount, 1);
    } else
        uatomic_store(&pic->shared->refcount, 1);

    size_t hmsize = hsize / pic_mgr->common_mgr.macropixel;
    size_t buffer_size = 0;
    size_t plane_sizes[pic_mgr->common_mgr.nb_planes];
    size_t strides[pic_mgr->common_mgr.nb_planes];
    for (uint8_t plane = 0; plane < pic_mgr->common_mgr.nb_planes; plane++) {
        size_t align = 0;
        if (pic_mgr->align &&
                ((hmsize + pic_mgr->hmprepend + pic_mgr->hmappend) /
                    pic_mgr->common_mgr.planes[plane]->hsub *
                    pic_mgr->common_mgr.planes[plane]->macropixel_size) %
                        pic_mgr->align)
            align = pic_mgr->align;
        strides[plane] = (hmsize + pic_mgr->hmprepend + pic_mgr->hmappend) /
                            pic_mgr->common_mgr.planes[plane]->hsub *
                            pic_mgr->common_mgr.planes[plane]->macropixel_size +
                         align;
        if (align)
            strides[plane] -= strides[plane] % align;
        plane_sizes[plane] = (vsize + pic_mgr->vprepend + pic_mgr->vappend) /
                                 pic_mgr->common_mgr.planes[plane]->vsub *
                                 strides[plane] + pic_mgr->align;
        buffer_size += plane_sizes[plane];
    }

    if (unlikely(!umem_alloc(pic_mgr->umem_mgr, &pic->shared->umem,
                             buffer_size))) {
        if (unlikely(!ulifo_push(&pic_mgr->shared_pool, pic->shared)))
            ubuf_pic_mem_shared_free_inner(pic->shared);
        if (unlikely(!ulifo_push(&pic_mgr->ubuf_pool, ubuf)))
            ubuf_pic_mem_free_inner(ubuf);
        return NULL;
    }
    ubuf_pic_common_init(ubuf, pic_mgr->hmprepend, pic_mgr->hmappend, hmsize,
                         pic_mgr->vprepend, pic_mgr->vappend, vsize);

    uint8_t *buffer = ubuf_pic_mem_buffer(ubuf);
    for (uint8_t plane = 0; plane < pic_mgr->common_mgr.nb_planes; plane++) {
        uint8_t *plane_buffer = buffer + pic_mgr->align;
        if (pic_mgr->align)
            plane_buffer -=
                ((uintptr_t)plane_buffer +
                 (pic_mgr->align_hmoffset + pic_mgr->hmprepend) /
                    pic_mgr->common_mgr.planes[plane]->hsub *
                    pic_mgr->common_mgr.planes[plane]->macropixel_size) %
                pic_mgr->align;
        ubuf_pic_common_plane_init(ubuf, plane, plane_buffer, strides[plane]);
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
static enum ubase_err ubuf_pic_mem_dup(struct ubuf *ubuf,
                                       struct ubuf **new_ubuf_p)
{
    assert(new_ubuf_p != NULL);
    struct ubuf *new_ubuf = ubuf_pic_mem_alloc_inner(ubuf->mgr);
    if (unlikely(new_ubuf == NULL))
        return UBASE_ERR_ALLOC;

    if (unlikely(!ubase_check(ubuf_pic_common_dup(ubuf, new_ubuf)))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    struct ubuf_pic_mem_mgr *pic_mgr =
        ubuf_pic_mem_mgr_from_ubuf_mgr(ubuf->mgr);
    for (uint8_t plane = 0; plane < pic_mgr->common_mgr.nb_planes; plane++) {
        if (unlikely(!ubase_check(ubuf_pic_common_plane_dup(ubuf, new_ubuf, plane)))) {
            ubuf_free(new_ubuf);
            return UBASE_ERR_INVALID;
        }
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
    struct ubuf_pic_mem *new_pic = ubuf_pic_mem_from_ubuf(new_ubuf);
    new_pic->shared = pic->shared;
    ubuf_pic_mem_use(new_ubuf);
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
static enum ubase_err ubuf_pic_mem_control(struct ubuf *ubuf,
                                           enum ubuf_command command,
                                           va_list args)
{
    switch (command) {
        case UBUF_DUP: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            return ubuf_pic_mem_dup(ubuf, new_ubuf_p);
        }
        case UBUF_SIZE_PICTURE: {
            size_t *hsize_p = va_arg(args, size_t *);
            size_t *vsize_p = va_arg(args, size_t *);
            uint8_t *macropixel_p = va_arg(args, uint8_t *);
            return ubuf_pic_common_size(ubuf, hsize_p, vsize_p, macropixel_p);
        }
        case UBUF_ITERATE_PICTURE_PLANE: {
            const char **chroma_p = va_arg(args, const char **);
            return ubuf_pic_common_plane_iterate(ubuf, chroma_p);
        }
        case UBUF_SIZE_PICTURE_PLANE: {
            const char *chroma = va_arg(args, const char *);
            size_t *stride_p = va_arg(args, size_t *);
            uint8_t *hsub_p = va_arg(args, uint8_t *);
            uint8_t *vsub_p = va_arg(args, uint8_t *);
            uint8_t *macropixel_size_p = va_arg(args, uint8_t *);
            return ubuf_pic_common_plane_size(ubuf, chroma, stride_p,
                                              hsub_p, vsub_p,
                                              macropixel_size_p);
        }
        case UBUF_READ_PICTURE_PLANE: {
            const char *chroma = va_arg(args, const char *);
            int hoffset = va_arg(args, int);
            int voffset = va_arg(args, int);
            int hsize = va_arg(args, int);
            int vsize = va_arg(args, int);
            uint8_t **buffer_p = va_arg(args, uint8_t **);
            enum ubase_err err =
                ubuf_pic_common_plane_map(ubuf, chroma, hoffset, voffset,
                                          hsize, vsize, buffer_p);
#ifndef NDEBUG
            if (ubase_check(err)) {
                struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
                uatomic_fetch_add(&pic->readers, 1);
            }
#endif
            return err;
        }
        case UBUF_WRITE_PICTURE_PLANE: {
            const char *chroma = va_arg(args, const char *);
            int hoffset = va_arg(args, int);
            int voffset = va_arg(args, int);
            int hsize = va_arg(args, int);
            int vsize = va_arg(args, int);
            uint8_t **buffer_p = va_arg(args, uint8_t **);
            if (!ubuf_pic_mem_single(ubuf))
                return UBASE_ERR_BUSY;
            enum ubase_err err =
                ubuf_pic_common_plane_map(ubuf, chroma, hoffset, voffset,
                                          hsize, vsize, buffer_p);
#ifndef NDEBUG
            if (ubase_check(err)) {
                struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
                uatomic_fetch_add(&pic->readers, 1);
            }
#endif
            return err;
        }
        case UBUF_UNMAP_PICTURE_PLANE: {
            /* we don't actually care about the parameters */
#ifndef NDEBUG
            struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
            uatomic_fetch_sub(&pic->readers, 1);
#endif
            return UBASE_ERR_NONE;
        }
        case UBUF_RESIZE_PICTURE: {
            /* Implementation note: here we agree to extend the ubuf, even
             * if the ubuf buffer is shared. Anyway a subsequent call to
             * @ref ubuf_pic_plane_write would fail and imply a buffer copy,
             * so it doesn't matter. */
            int hskip = va_arg(args, int);
            int vskip = va_arg(args, int);
            int new_hsize = va_arg(args, int);
            int new_vsize = va_arg(args, int);
            return ubuf_pic_common_resize(ubuf, hskip, vskip,
                                          new_hsize, new_vsize);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees a ubuf and all associated data structures.
 *
 * @param ubuf pointer to a ubuf structure to free
 */
static void ubuf_pic_mem_free_inner(struct ubuf *ubuf)
{
    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);
#ifndef NDEBUG
    uatomic_clean(&pic->readers);
#endif
    free(pic);
}

/** @internal @This frees a shared buffer.
 *
 * @param shared pointer to shared structure to free
 */
static void ubuf_pic_mem_shared_free_inner(struct ubuf_pic_mem_shared *shared)
{
    free(shared);
}

/** @This recycles or frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure
 */
static void ubuf_pic_mem_free(struct ubuf *ubuf)
{
    struct ubuf_pic_mem_mgr *pic_mgr =
        ubuf_pic_mem_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_mem *pic = ubuf_pic_mem_from_ubuf(ubuf);

    ubuf_pic_common_clean(ubuf);
    for (uint8_t plane = 0; plane < pic_mgr->common_mgr.nb_planes; plane++)
        ubuf_pic_common_plane_clean(ubuf, plane);

#ifndef NDEBUG
    assert(uatomic_load(&pic->readers) == 0);
#endif

    if (unlikely(uatomic_fetch_sub(&pic->shared->refcount, 1) == 1)) {
        umem_free(&pic->shared->umem);
        if (unlikely(!ulifo_push(&pic_mgr->shared_pool, pic->shared)))
            ubuf_pic_mem_shared_free_inner(pic->shared);
    }

    if (unlikely(!ulifo_push(&pic_mgr->ubuf_pool, ubuf)))
        ubuf_pic_mem_free_inner(ubuf);

    ubuf_mgr_release(ubuf_pic_mem_mgr_to_ubuf_mgr(pic_mgr));
}

/** @internal @This instructs an existing ubuf pic manager to release all
 * structures currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a ubuf manager
 */
static void ubuf_pic_mem_mgr_vacuum(struct ubuf_mgr *mgr)
{
    struct ubuf_pic_mem_mgr *pic_mgr = ubuf_pic_mem_mgr_from_ubuf_mgr(mgr);
    struct ubuf *ubuf;
    struct ubuf_pic_mem_shared *shared;

    while ((ubuf = ulifo_pop(&pic_mgr->ubuf_pool, struct ubuf *)) != NULL)
        ubuf_pic_mem_free_inner(ubuf);
    while ((shared = ulifo_pop(&pic_mgr->shared_pool,
                               struct ubuf_pic_mem_shared *)) != NULL)
        ubuf_pic_mem_shared_free_inner(shared);
}

/** @This handles manager control commands.
 *
 * @param mgr pointer to ubuf manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static enum ubase_err ubuf_pic_mem_mgr_control(struct ubuf_mgr *mgr,
                                               enum ubuf_mgr_command command,
                                               va_list args)
{
    switch (command) {
        case UBUF_MGR_VACUUM: {
            ubuf_pic_mem_mgr_vacuum(mgr);
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
static void ubuf_pic_mem_mgr_free(struct urefcount *urefcount)
{
    struct ubuf_pic_mem_mgr *pic_mgr =
        ubuf_pic_mem_mgr_from_urefcount(urefcount);
    ubuf_pic_mem_mgr_vacuum(ubuf_pic_mem_mgr_to_ubuf_mgr(pic_mgr));
    ulifo_clean(&pic_mgr->ubuf_pool);
    ulifo_clean(&pic_mgr->shared_pool);
    umem_mgr_release(pic_mgr->umem_mgr);

    ubuf_pic_common_mgr_clean(ubuf_pic_mem_mgr_to_ubuf_mgr(pic_mgr));

    urefcount_clean(urefcount);
    free(pic_mgr);
}

/** @This allocates a new instance of the ubuf manager for picture formats
 * using umem.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param macropixel number of pixels in a macropixel (typically 1)
 * @param hprepend extra pixels added before each line (if set to -1, a
 * default sensible value is used)
 * @param happend extra pixels added after each line (if set to -1, a
 * default sensible value is used)
 * @param vprepend extra lines added before buffer (if set to -1, a
 * default sensible value is used)
 * @param vappend extra lines added after buffer (if set to -1, a
 * default sensible value is used)
 * @param align alignment in octets (if set to 0, no line will be voluntarily
 * aligned)
 * @param align_hmoffset horizontal offset of the aligned macropixel in a line
 * (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_pic_mem_mgr_alloc(uint16_t ubuf_pool_depth,
                                        uint16_t shared_pool_depth,
                                        struct umem_mgr *umem_mgr,
                                        uint8_t macropixel,
                                        int hprepend, int happend,
                                        int vprepend, int vappend,
                                        int align, int align_hmoffset)
{
    assert(umem_mgr != NULL);
    assert(hprepend == -1 || !(hprepend % macropixel));
    assert(happend == -1 || !(happend % macropixel));

    struct ubuf_pic_mem_mgr *pic_mgr =
        malloc(sizeof(struct ubuf_pic_mem_mgr) +
               ulifo_sizeof(ubuf_pool_depth) +
               ulifo_sizeof(shared_pool_depth));
    if (unlikely(pic_mgr == NULL))
        return NULL;

    ulifo_init(&pic_mgr->ubuf_pool, ubuf_pool_depth,
               (void *)pic_mgr + sizeof(struct ubuf_pic_mem_mgr));
    ulifo_init(&pic_mgr->shared_pool, shared_pool_depth,
               (void *)pic_mgr + sizeof(struct ubuf_pic_mem_mgr) +
               ulifo_sizeof(ubuf_pool_depth));
    pic_mgr->umem_mgr = umem_mgr;
    umem_mgr_use(umem_mgr);

    struct ubuf_mgr *mgr = ubuf_pic_mem_mgr_to_ubuf_mgr(pic_mgr);
    ubuf_pic_common_mgr_init(mgr, macropixel);

    pic_mgr->hmprepend = (hprepend >= 0 ? hprepend : UBUF_DEFAULT_HPREPEND) /
                         macropixel;
    pic_mgr->hmappend = (happend >= 0 ? happend : UBUF_DEFAULT_HAPPEND) /
                        macropixel;
    pic_mgr->vprepend = vprepend >= 0 ? vprepend : UBUF_DEFAULT_VPREPEND;
    pic_mgr->vappend = vappend >= 0 ? vappend : UBUF_DEFAULT_VAPPEND;
    pic_mgr->align = align >= 0 ? align : UBUF_DEFAULT_ALIGN;
    pic_mgr->align_hmoffset = align_hmoffset;

    urefcount_init(ubuf_pic_mem_mgr_to_urefcount(pic_mgr),
                   ubuf_pic_mem_mgr_free);
    pic_mgr->common_mgr.mgr.refcount = ubuf_pic_mem_mgr_to_urefcount(pic_mgr);

    mgr->type = UBUF_ALLOC_PICTURE;
    mgr->ubuf_alloc = ubuf_pic_mem_alloc;
    mgr->ubuf_control = ubuf_pic_mem_control;
    mgr->ubuf_free = ubuf_pic_mem_free;
    mgr->ubuf_mgr_control = ubuf_pic_mem_mgr_control;

    return mgr;
}

/** @This adds a new plane to a ubuf manager for picture formats using umem.
 * It may only be called on initializing the manager, before any ubuf is
 * allocated.
 *
 * @param mgr pointer to a ubuf_mgr structure
 * @param chroma chroma type (see chroma reference)
 * @param hsub horizontal subsamping
 * @param vsub vertical subsamping
 * @param macropixel_size size of a macropixel in octets
 * @return an error code
 */
enum ubase_err ubuf_pic_mem_mgr_add_plane(struct ubuf_mgr *mgr,
                                          const char *chroma,
                                          uint8_t hsub, uint8_t vsub,
                                          uint8_t macropixel_size)
{
    assert(mgr != NULL);
    ubuf_pic_mem_mgr_vacuum(mgr);

    return ubuf_pic_common_mgr_add_plane(mgr, chroma, hsub, vsub,
                                         macropixel_size);
}

/** @This allocates a new instance of the ubuf manager for picture formats
 * using umem, from a fourcc image format.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param fcc fourcc to use to create the manager
 * @param hmprepend extra macropixels added before each line (if set to -1, a
 * default sensible value is used)
 * @param hmappend extra macropixels added after each line (if set to -1, a
 * default sensible value is used)
 * @param vprepend extra lines added before buffer (if set to -1, a
 * default sensible value is used)
 * @param vappend extra lines added after buffer (if set to -1, a
 * default sensible value is used)
 * @param align alignment in octets (if set to 0, no line will be voluntarily
 * aligned)
 * @param align_hmoffset horizontal offset of the aligned macropixel in a line
 * (may be negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_pic_mem_mgr_alloc_fourcc(uint16_t ubuf_pool_depth,
                                               uint16_t shared_pool_depth,
                                               struct umem_mgr *umem_mgr,
                                               const char *fcc,
                                               int hmprepend, int hmappend,
                                               int vprepend, int vappend,
                                               int align, int align_hmoffset)
{
    struct ubuf_mgr *mgr = NULL;
    assert(fcc != NULL);

    /* YUV planar formats */
    if (!strcmp(fcc, "I420") || !strcmp(fcc, "YV12") || !strcmp(fcc, "IYUV")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 1,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "y8", 1, 1, 1) ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "u8", 2, 2, 1) ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "v8", 2, 2, 1)))
            goto fourcc_err;

    } else if (!strcmp(fcc, "YV16")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 1,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "y8", 1, 1, 1) ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "u8", 2, 1, 1) ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "v8", 2, 1, 1)))
            goto fourcc_err;

    /* YUV packed formats */
    } else if (!strcmp(fcc, "YUVY") || !strcmp(fcc, "YUY2") ||
               !strcmp(fcc, "YUNV") || !strcmp(fcc, "V422")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 2,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "y8u8y8v8", 1, 1, 4)))
            goto fourcc_err;

    } else if (!strcmp(fcc, "UYVY")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 2,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "u8y8v8y8", 1, 1, 4)))
            goto fourcc_err;

    } else if (!strcmp(fcc, "YVYU")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 2,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "y8v8y8u8", 1, 1, 4)))
            goto fourcc_err;

    } else if (!strcmp(fcc, "AYUV")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 1,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "a8y8u8v8", 1, 1, 4)))
            goto fourcc_err;

    } else if (!strcmp(fcc, "V410")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 1,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "u10y10v10", 1, 1, 4)))
            goto fourcc_err;

    /* RGB packed formats */
    } else if (!strcmp(fcc, "RGBA")) {
        mgr = ubuf_pic_mem_mgr_alloc(ubuf_pool_depth, shared_pool_depth,
                                     umem_mgr, 1,
                                     hmprepend, hmappend, vprepend, vappend,
                                     align, align_hmoffset);
        if (unlikely(mgr == NULL ||
                     !ubuf_pic_mem_mgr_add_plane(mgr, "a8r8g8b8", 1, 1, 4)))
            goto fourcc_err;
    }

    return mgr;

fourcc_err:
    if (mgr)
        ubuf_mgr_release(mgr);
    return NULL;
}

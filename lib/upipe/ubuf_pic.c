/*****************************************************************************
 * ubuf_pic.c: struct ubuf manager for picture formats
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
 * NB: Not all picture managers are compatible in the manner requested by
 * ubuf_writable() and ubuf_pic_resize() for the new manager. If two different
 * managers are used, they can only differ in the prepend, append, align and
 * align_hmoffset options, and may obviously not have different plane
 * numbers, subsampling or macropixel options.
 */

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/ulifo.h>
#include <upipe/ubuf_pic.h>

#include <stdlib.h>
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
#define UBUF_DEFAULT_ALIGN          16

/** sub-structure describing the allocation of individuals planes */
struct ubuf_pic_mgr_plane {
    /** horizontal subsampling */
    uint8_t hsub;
    /** vertical subsampling */
    uint8_t vsub;
    /** macropixel size */
    uint8_t macropixel_size;
};

/** super-set of the ubuf_mgr structure with additional local members */
struct ubuf_pic_mgr {
    /** number of pixels in a macropixel */
    uint8_t macropixel;
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

    /** number of planes to allocate */
    uint8_t nb_planes;
    /** planes description */
    struct ubuf_pic_mgr_plane **planes;

    /** ubuf pool */
    struct ulifo pool;

    /** common management structure */
    struct ubuf_mgr mgr;
};

/** super-set of the ubuf structure with additional local members */
struct ubuf_pic {
    /** extra space allocated at the end of the structure */
    size_t extra_space;
    /** requested horizontal number of macropixels */
    size_t hmsize;
    /** requested vertical number of lines */
    size_t vsize;
    /** extra macropixels added before lines */
    size_t hmprepend;
    /** extra macropixels added after lines */
    size_t hmappend;
    /** extra lines added before buffer */
    size_t vprepend;
    /** extra lines added after buffer */
    size_t vappend;

    /** common structure */
    struct ubuf ubuf;
};

/** @internal @This returns the high-level ubuf structure.
 *
 * @param pic pointer to the ubuf_pic structure
 * @return pointer to the ubuf structure
 */
static inline struct ubuf *ubuf_pic_to_ubuf(struct ubuf_pic *pic)
{
    return &pic->ubuf;
}

/** @internal @This returns the private ubuf_pic structure.
 *
 * @param mgr description structure of the ubuf mgr
 * @return pointer to the ubuf_pic structure
 */
static inline struct ubuf_pic *ubuf_pic_from_ubuf(struct ubuf *ubuf)
{
    return container_of(ubuf, struct ubuf_pic, ubuf);
}

/** @internal @This returns the high-level ubuf_mgr structure.
 *
 * @param pic_mgr pointer to the ubuf_pic_mgr structure
 * @return pointer to the ubuf_mgr structure
 */
static inline struct ubuf_mgr *ubuf_pic_mgr_to_ubuf_mgr(struct ubuf_pic_mgr *pic_mgr)
{
    return &pic_mgr->mgr;
}

/** @internal @This returns the private ubuf_pic_mgr structure.
 *
 * @param mgr description structure of the ubuf mgr
 * @return pointer to the ubuf_pic_mgr structure
 */
static inline struct ubuf_pic_mgr *ubuf_pic_mgr_from_ubuf_mgr(struct ubuf_mgr *mgr)
{
    return container_of(mgr, struct ubuf_pic_mgr, mgr);
}

/** @internal @This checks whether the requested picture size can be
 * allocated with this manager.
 *
 * @param mgr common management structure
 * @param hsize horizontal size in pixels
 * @param vsize vertical size in lines
 * @return true if the picture size can be allocated
 */
static bool ubuf_pic_check_size(struct ubuf_mgr *mgr, int hsize, int vsize)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    if (unlikely(hsize <= 0 || vsize <= 0))
        return false;

    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        size_t hgran = pic_mgr->macropixel * pic_mgr->planes[plane]->hsub;
        size_t vgran = pic_mgr->planes[plane]->vsub;
        if (unlikely(hsize % hgran || vsize % vgran))
            return false;
    }
    return true;
}

/** @internal @This checks whether the requested picture resize can be
 * performed with this manager.
 *
 * @param mgr common management structure
 * @param hskip
 * @param vskip
 * @return true if the picture size can be allocated
 */
static bool ubuf_pic_check_skip(struct ubuf_mgr *mgr, int hskip, int vskip)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    if (unlikely(hskip < 0)) hskip = -hskip;
    if (unlikely(vskip < 0)) vskip = -vskip;

    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        size_t hgran = pic_mgr->macropixel * pic_mgr->planes[plane]->hsub;
        size_t vgran = pic_mgr->planes[plane]->vsub;
        if (unlikely(hskip % hgran || vskip % vgran))
            return false;
    }
    return true;
}

/** @internal @This allocates the data structure or fetches it from the pool.
 *
 * @param mgr common management structure
 * @param hmsize horizontal size in macropixels
 * @param vsize vertical size in lines
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_pic_alloc_inner(struct ubuf_mgr *mgr, size_t hmsize,
                                         size_t vsize)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    size_t extra_space = pic_mgr->align;
    size_t vstrides[pic_mgr->nb_planes];
    size_t plane_sizes[pic_mgr->nb_planes];
    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        vstrides[plane] = (hmsize + pic_mgr->hmprepend + pic_mgr->hmappend) /
                              pic_mgr->planes[plane]->hsub *
                              pic_mgr->planes[plane]->macropixel_size;
        plane_sizes[plane] = (vsize + pic_mgr->vprepend + pic_mgr->vappend) /
                                 pic_mgr->planes[plane]->vsub * vstrides[plane];
        extra_space += plane_sizes[plane];
    }

    struct ubuf_pic *pic = NULL;
    struct uchain *uchain = ulifo_pop(&pic_mgr->pool, struct uchain *);
    if (likely(uchain != NULL))
        pic = ubuf_pic_from_ubuf(ubuf_from_uchain(uchain));

    if (unlikely(pic == NULL)) {
        pic = malloc(sizeof(struct ubuf_pic) +
                     sizeof(struct ubuf_plane) * pic_mgr->nb_planes +
                     extra_space);
        if (unlikely(pic == NULL)) return NULL;
        pic->extra_space = extra_space;
        pic->ubuf.mgr = mgr;
    } else if (unlikely(pic->extra_space < extra_space)) {
        struct ubuf_pic *old_pic = pic;
        pic = realloc(pic, sizeof(struct ubuf_pic) +
                           sizeof(struct ubuf_plane) * pic_mgr->nb_planes +
                           extra_space);
        if (unlikely(pic == NULL)) {
            free(old_pic);
            return NULL;
        }
        pic->extra_space = extra_space;
    }

    pic->hmsize = hmsize;
    pic->vsize = vsize;
    pic->hmprepend = pic_mgr->hmprepend;
    pic->hmappend = pic_mgr->hmappend;
    pic->vprepend = pic_mgr->vprepend;
    pic->vappend = pic_mgr->vappend;

    uint8_t *buffer = (uint8_t *)pic + sizeof(struct ubuf_pic) +
                      sizeof(struct ubuf_plane) * pic_mgr->nb_planes;
    struct ubuf *ubuf = ubuf_pic_to_ubuf(pic);
    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        ubuf->planes[plane].stride = vstrides[plane];
        ubuf->planes[plane].buffer =
            buffer + pic_mgr->vprepend * vstrides[plane] +
            pic_mgr->hmprepend / pic_mgr->planes[plane]->hsub *
                pic_mgr->planes[plane]->macropixel_size + pic_mgr->align;
        ubuf->planes[plane].buffer -=
            ((uintptr_t)ubuf->planes[0].buffer +
             pic_mgr->align_hmoffset / pic_mgr->planes[plane]->hsub *
             pic_mgr->planes[plane]->macropixel_size) % pic_mgr->align;
        buffer += plane_sizes[plane];
    }
    urefcount_init(&ubuf->refcount);
    ubuf_mgr_use(mgr);

    uchain_init(&ubuf->uchain);
    return ubuf;
}

/** @This allocates a ubuf and a picture buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_TYPE_PICTURE (sentinel)
 * @param args optional arguments (1st = horizontal size, 2nd = vertical size)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *_ubuf_pic_alloc(struct ubuf_mgr *mgr,
                                    enum ubuf_alloc_type alloc_type,
                                    va_list args)
{
    assert(alloc_type == UBUF_ALLOC_TYPE_PICTURE);
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    int hsize;
    int vsize;

    /* Parse arguments */
    hsize = va_arg(args, int);
    vsize = va_arg(args, int);
    if (unlikely(!ubuf_pic_check_size(mgr, hsize, vsize)))
        return NULL;

    return ubuf_pic_alloc_inner(mgr, hsize / pic_mgr->macropixel, vsize);
}

/** @This duplicates a ubuf and its picture buffer.
 *
 * @param ubuf ubuf structure to duplicate
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *_ubuf_pic_dup(struct ubuf_mgr *mgr, struct ubuf *ubuf)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    struct ubuf_pic *pic = ubuf_pic_from_ubuf(ubuf);
    size_t hmsize = pic->hmsize;
    size_t vsize = pic->vsize;
    struct ubuf *new_ubuf = ubuf_pic_alloc_inner(mgr, hmsize, vsize);
    if (unlikely(new_ubuf == NULL)) return NULL;

    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        size_t line_size = hmsize / pic_mgr->planes[plane]->hsub *
                           pic_mgr->planes[plane]->macropixel_size;
        size_t lines = vsize / pic_mgr->planes[plane]->vsub;
        uint8_t *buffer = ubuf->planes[plane].buffer;
        uint8_t *new_buffer = new_ubuf->planes[plane].buffer;
        for (size_t line = 0; line < lines; line++) {
            memcpy(new_buffer, buffer, line_size);
            buffer += ubuf->planes[plane].stride;
            new_buffer += new_ubuf->planes[plane].stride;
        }
    }

    return new_ubuf;
}

/** @internal @This frees a ubuf and all associated data structures.
 *
 * @param pic pointer to a ubuf_pic structure to free
 */
static void _ubuf_pic_free_inner(struct ubuf_pic *pic)
{
    urefcount_clean(&pic->ubuf.refcount);
    free(pic);
}

/** @This frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure to free
 */
static void _ubuf_pic_free(struct ubuf *ubuf)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic *pic = ubuf_pic_from_ubuf(ubuf);

    if (likely(ulifo_push(&pic_mgr->pool, &ubuf->uchain)))
        pic = NULL;
    if (unlikely(pic != NULL))
        _ubuf_pic_free_inner(pic);

    ubuf_mgr_release(&pic_mgr->mgr);
}

/** @internal @This re-allocates a ubuf with less or more space.
 *
 * @param mgr management structure used to create a new buffer
 * @param ubuf_p reference to a pointer to a ubuf to resize
 * @param new_hsize final horizontal size of the buffer, in pixels
 * @param new_hsize final horizontal size of the buffer, in macropixels
 * @param new_vsize final vertical size of the buffer, in lines
 * @param hskip number of macropixels to skip at the beginning of each line
 * (if < 0, extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @return false in case of allocation error (unchanged uref)
 */
static bool ubuf_pic_realloc(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                             int new_hsize, int new_hmsize, int new_vsize,
                             int hmskip, int vskip)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    assert(mgr != NULL);
    struct ubuf *ubuf = *ubuf_p;
    struct ubuf_pic *pic = ubuf_pic_from_ubuf(ubuf);
    struct ubuf *new_ubuf = ubuf_pic_alloc(mgr, new_hsize, new_vsize);
    if (unlikely(new_ubuf == NULL)) return false;

    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        uint8_t *buffer = ubuf->planes[plane].buffer;
        uint8_t *new_buffer = new_ubuf->planes[plane].buffer;
        size_t line_size;
        size_t lines;

        if (likely(vskip >= 0)) {
            buffer += vskip * ubuf->planes[plane].stride /
                      pic_mgr->planes[plane]->vsub;
            lines = likely(new_vsize <= pic->vsize - vskip) ?
                        new_vsize : pic->vsize - vskip;
        } else {
            new_buffer += -vskip * new_ubuf->planes[plane].stride /
                          pic_mgr->planes[plane]->vsub;
            lines = likely(new_vsize + vskip <= pic->vsize) ?
                        new_vsize + vskip : pic->vsize;
        }
        lines /= pic_mgr->planes[plane]->vsub;

        if (likely(hmskip >= 0)) {
            buffer += hmskip * pic_mgr->planes[plane]->macropixel_size /
                      pic_mgr->planes[plane]->hsub;
            line_size = likely(new_hmsize <= pic->hmsize - hmskip) ?
                            new_hmsize : pic->hmsize - hmskip;
        } else {
            new_buffer += -hmskip * pic_mgr->planes[plane]->macropixel_size /
                          pic_mgr->planes[plane]->hsub;
            line_size = likely(new_hmsize + hmskip <= pic->hmsize) ?
                            new_hmsize + hmskip : pic->hmsize;
        }
        line_size *= pic_mgr->planes[plane]->macropixel_size;
        line_size /= pic_mgr->planes[plane]->hsub;

        for (size_t line = 0; line < lines; line++) {
            memcpy(new_buffer, buffer, line_size);
            buffer += ubuf->planes[plane].stride;
            new_buffer += new_ubuf->planes[plane].stride;
        }
    }

    ubuf_release(ubuf);
    *ubuf_p = new_ubuf;
    return true;
}

/** @This resizes or re-allocates a ubuf with less or more space.
 *
 * @param mgr management structure used to create a new buffer, if needed
 * (can be NULL if ubuf_single(ubuf) and requested picture is smaller than
 * the original)
 * @param ubuf_p reference to a pointer to a ubuf to resize
 * @param alloc_type must be UBUF_ALLOC_TYPE_PICTURE (sentinel)
 * @param args optional arguments (1st and 2nd = horizontal and vertical
 * size in pixels after operation, 3rd and 4th = number of pixels / lines to
 * skip at the beginning, can be negative)
 * @return false in case of allocation error (unchanged uref_t)
 */
static bool _ubuf_pic_resize(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                             enum ubuf_alloc_type alloc_type, va_list args)
{
    assert(alloc_type == UBUF_ALLOC_TYPE_PICTURE);
    struct ubuf *ubuf = *ubuf_p;
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic *pic = ubuf_pic_from_ubuf(ubuf);
    int new_hsize = -1, new_vsize = -1, new_hmsize;
    int hskip, hmskip, vskip;
    int arg;

    /* Parse arguments */
    arg = va_arg(args, int); if (arg >= 0) new_hsize = arg;
    arg = va_arg(args, int); if (arg >= 0) new_vsize = arg;
    arg = va_arg(args, int); hskip = arg;
    arg = va_arg(args, int); vskip = arg;
    if (unlikely(new_hsize == -1))
        new_hsize = pic->hmsize * pic_mgr->macropixel - hskip;
    if (unlikely(new_vsize == -1))
        new_vsize = pic->vsize - vskip;
    if (unlikely(!ubuf_pic_check_size(mgr, new_hsize, new_vsize) ||
                 !ubuf_pic_check_skip(mgr, hskip, vskip)))
        return NULL;
    new_hmsize = new_hsize / pic_mgr->macropixel;
    hmskip = hskip / pic_mgr->macropixel;

    if (unlikely(hmskip < 0 && new_hmsize < -hmskip)) return false;
    if (unlikely(vskip < 0 && new_vsize < -vskip)) return false;
    if (unlikely(hmskip >= 0 && pic->hmsize < hmskip)) return false;
    if (unlikely(vskip >= 0 && pic->vsize < vskip)) return false;

    /* if ubuf is in use, allocate a new one with the needed size */
    if (unlikely(!ubuf_single(ubuf)))
        return ubuf_pic_realloc(mgr, ubuf_p, new_hsize, new_hmsize, new_vsize,
                                hmskip, vskip);

    size_t hmoffset = pic->hmprepend;
    size_t hmhigh = pic->hmprepend + pic->hmsize + pic->hmappend;
    size_t voffset = pic->vprepend;
    size_t vhigh = pic->vprepend + pic->vsize + pic->vappend;

    /* try just changing the pointers */
    if (likely((int)hmoffset + hmskip >= 0 &&
               (int)hmoffset + hmskip + new_hmsize <= hmhigh &&
               (int)voffset + vskip >= 0 &&
               (int)voffset + vskip + new_vsize <= vhigh)) {
        for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++)
            ubuf->planes[plane].buffer +=
                vskip * (int)ubuf->planes[plane].stride /
                    pic_mgr->planes[plane]->vsub +
                hmskip * (int)pic_mgr->planes[plane]->macropixel_size /
                    pic_mgr->planes[plane]->hsub;
        pic->hmsize = new_hmsize;
        pic->vsize = new_vsize;
        pic->hmprepend += hmskip;
        pic->hmappend = hmhigh - pic->hmprepend - new_hmsize;
        pic->vprepend += vskip;
        pic->vappend = vhigh - pic->vprepend - new_vsize;
        return true;
    }

    return ubuf_pic_realloc(mgr, ubuf_p, new_hsize, new_hmsize, new_vsize,
                            hmskip, vskip);
}

/** @This frees a ubuf_mgr structure.
 *
 * @param mgr pointer to a ubuf_mgr structure to free
 */
static void _ubuf_pic_mgr_free(struct ubuf_mgr *mgr)
{
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    struct uchain *uchain;

    while ((uchain = ulifo_pop(&pic_mgr->pool, struct uchain *)) != NULL) {
        struct ubuf_pic *pic = ubuf_pic_from_ubuf(ubuf_from_uchain(uchain));
        _ubuf_pic_free_inner(pic);
    }
    ulifo_clean(&pic_mgr->pool);

    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++)
        free(pic_mgr->planes[plane]);
    free(pic_mgr->planes);

    urefcount_clean(&pic_mgr->mgr.refcount);
    free(pic_mgr);
}

/** @This allocates a new instance of the ubuf manager for picture formats.
 *
 * @param pool_depth maximum number of pictures in the pool
 * @param macropixel number of pixels in a macropixel (typically 1)
 * @param hmprepend extra macropixels added before each line (if set to -1, a
 * default sensible value is used)
 * @param hmappend extra macropixels added after each line (if set to -1, a
 * default sensible value is used)
 * @param vprepend extra lines added before buffer (if set to -1, a
 * default sensible value is used)
 * @param vappend extra lines added after buffer (if set to -1, a
 * default sensible value is used)
 * @param align alignment in octets (if set to -1, a default sensible
 * value is used)
 * @param align_hmoffset horizontal offset of the aligned macropixel (may be
 * negative)
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_pic_mgr_alloc(unsigned int pool_depth, uint8_t macropixel,
                                    int hmprepend, int hmappend,
                                    int vprepend, int vappend,
                                    int align, int align_hmoffset)
{
    if (unlikely(!macropixel)) return NULL;
    struct ubuf_pic_mgr *pic_mgr = malloc(sizeof(struct ubuf_pic_mgr) +
                                          ulifo_sizeof(pool_depth));
    if (unlikely(pic_mgr == NULL)) return NULL;

    pic_mgr->macropixel = macropixel;
    pic_mgr->hmprepend = hmprepend >= 0 ? hmprepend : UBUF_DEFAULT_HPREPEND;
    pic_mgr->hmappend = hmappend >= 0 ? hmappend : UBUF_DEFAULT_HAPPEND;
    pic_mgr->vprepend = vprepend >= 0 ? vprepend : UBUF_DEFAULT_VPREPEND;
    pic_mgr->vappend = vappend >= 0 ? vappend : UBUF_DEFAULT_VAPPEND;
    pic_mgr->align = align > 0 ? align : UBUF_DEFAULT_ALIGN;
    pic_mgr->align_hmoffset = align_hmoffset;

    ulifo_init(&pic_mgr->pool, pool_depth,
               (void *)pic_mgr + sizeof(struct ubuf_pic_mgr));
    pic_mgr->nb_planes = 0;
    pic_mgr->planes = NULL;

    urefcount_init(&pic_mgr->mgr.refcount);
    pic_mgr->mgr.ubuf_alloc = _ubuf_pic_alloc;
    pic_mgr->mgr.ubuf_dup = _ubuf_pic_dup;
    pic_mgr->mgr.ubuf_free = _ubuf_pic_free;
    pic_mgr->mgr.ubuf_resize = _ubuf_pic_resize;
    pic_mgr->mgr.ubuf_mgr_free = _ubuf_pic_mgr_free;

    return ubuf_pic_mgr_to_ubuf_mgr(pic_mgr);
}

/** @This adds a new plane to a ubuf manager for picture formats. It may
 * only be called on initializing the manager.
 *
 * @param mgr pointer to a ubuf_mgr structure
 * @param hsub horizontal subsamping
 * @param vsub vertical subsamping
 * @param macropixel_size size of a macropixel in octets
 * @return false in case of error
 */
bool ubuf_pic_mgr_add_plane(struct ubuf_mgr *mgr, uint8_t hsub, uint8_t vsub,
                            uint8_t macropixel_size)
{
    assert(mgr != NULL);
    struct ubuf_pic_mgr *pic_mgr = ubuf_pic_mgr_from_ubuf_mgr(mgr);
    if (unlikely(!hsub || !vsub || !macropixel_size)) return false;
    struct ubuf_pic_mgr_plane *plane = malloc(sizeof(struct ubuf_pic_mgr_plane));
    if (unlikely(plane == NULL)) return false;

    plane->hsub = hsub;
    plane->vsub = vsub;
    plane->macropixel_size = macropixel_size;

    struct ubuf_pic_mgr_plane **planes = realloc(pic_mgr->planes,
                (pic_mgr->nb_planes + 1) * sizeof(struct ubuf_pic_mgr_plane *));
    if (unlikely(planes == NULL)) {
        free(plane);
        return false;
    }
    pic_mgr->planes = planes;
    pic_mgr->planes[pic_mgr->nb_planes++] = plane;
    return true;
}

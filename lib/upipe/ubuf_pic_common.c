/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_common.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

/** @This checks whether the requested picture size can be allocated with the
 * manager.
 *
 * @param mgr common management structure
 * @param hsize horizontal size in pixels
 * @param vsize vertical size in lines
 * @return an error code
 */
int ubuf_pic_common_check_size(struct ubuf_mgr *mgr, int hsize, int vsize)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);
    if (unlikely(hsize <= 0 || vsize <= 0))
        return UBASE_ERR_INVALID;

    for (uint8_t plane = 0; plane < common_mgr->nb_planes; plane++) {
        size_t hgran = common_mgr->macropixel * common_mgr->planes[plane]->hsub;
        size_t vgran = common_mgr->planes[plane]->vsub;
        if (unlikely(hsize % hgran || vsize % vgran))
            return UBASE_ERR_INVALID;
    }
    return UBASE_ERR_NONE;
}

/** @This duplicates the content of the common structure for picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @return an error code
 */
int ubuf_pic_common_dup(struct ubuf *ubuf, struct ubuf *new_ubuf)
{
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    struct ubuf_pic_common *new_common =
        ubuf_pic_common_from_ubuf(new_ubuf);
    new_common->hmprepend = common->hmprepend;
    new_common->hmappend = common->hmappend;
    new_common->hmsize = common->hmsize;
    new_common->vprepend = common->vprepend;
    new_common->vappend = common->vappend;
    new_common->vsize = common->vsize;
    return UBASE_ERR_NONE;
}

/** @This duplicates the content of the plane sub-structure for picture ubuf.
 * It is only necessary to call this function if you plan to use
 * @ref ubuf_pic_common_plane_map.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @param plane index of the plane
 * @return an error code
 */
int ubuf_pic_common_plane_dup(struct ubuf *ubuf, struct ubuf *new_ubuf,
                              uint8_t plane)
{
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    struct ubuf_pic_common *new_common =
        ubuf_pic_common_from_ubuf(new_ubuf);
    new_common->planes[plane].buffer = common->planes[plane].buffer;
    new_common->planes[plane].stride = common->planes[plane].stride;
    return UBASE_ERR_NONE;
}

/** @This returns the sizes of the picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param hsize_p reference written with the horizontal size of the picture
 * if not NULL
 * @param vsize_p reference written with the vertical size of the picture
 * if not NULL
 * @param macropixel_p reference written with the number of pixels in a
 * macropixel if not NULL
 * @return an error code
 */
int ubuf_pic_common_size(struct ubuf *ubuf, size_t *hsize_p, size_t *vsize_p,
                         uint8_t *macropixel_p)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    if (likely(hsize_p != NULL))
        *hsize_p = common->hmsize * common_mgr->macropixel;
    if (likely(vsize_p != NULL))
        *vsize_p = common->vsize;
    if (likely(macropixel_p != NULL))
        *macropixel_p = common_mgr->macropixel;
    return UBASE_ERR_NONE;
}

/** @This iterates on picture planes chroma types. Start by initializing
 * *chroma_p to NULL. If *chroma_p is NULL after running this function, there
 * are no more planes in this picture. Otherwise the string pointed to by
 * *chroma_p remains valid until the ubuf picture manager is deallocated.
 *
 * @param ubuf pointer to ubuf
 * @param chroma_p reference written with chroma type of the next plane
 * @return an error code
 */
int ubuf_pic_common_iterate_plane(struct ubuf *ubuf, const char **chroma_p)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(ubuf->mgr);
    int plane;
    if (*chroma_p != NULL) {
        plane = ubuf_pic_common_plane(ubuf->mgr, *chroma_p);
        if (unlikely(plane < 0))
            return UBASE_ERR_INVALID;
        plane++;
    } else
        plane = 0;

    if (plane < common_mgr->nb_planes)
        *chroma_p = common_mgr->planes[plane]->chroma;
    else
        *chroma_p = NULL;
    return UBASE_ERR_NONE;
}

/** @This returns the sizes of a plane of the picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param stride_p reference written with the offset between lines, in octets,
 * if not NULL
 * @param hsub_p reference written with the horizontal subsamping for this plane
 * if not NULL
 * @param vsub_p reference written with the vertical subsamping for this plane
 * if not NULL
 * @param macropixel_size_p reference written with the size of a macropixel in
 * octets for this plane if not NULL
 * @return an error code
 */
int ubuf_pic_common_plane_size(struct ubuf *ubuf, const char *chroma,
                               size_t *stride_p,
                               uint8_t *hsub_p, uint8_t *vsub_p,
                               uint8_t *macropixel_size_p)
{
    assert(chroma != NULL);
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    int plane = ubuf_pic_common_plane(ubuf->mgr, chroma);
    if (unlikely(plane < 0))
        return UBASE_ERR_INVALID;
    if (likely(stride_p != NULL))
        *stride_p = common->planes[plane].stride;
    if (likely(hsub_p != NULL))
        *hsub_p = common_mgr->planes[plane]->hsub;
    if (likely(vsub_p != NULL))
        *vsub_p = common_mgr->planes[plane]->vsub;
    if (likely(macropixel_size_p != NULL))
        *macropixel_size_p = common_mgr->planes[plane]->macropixel_size;
    return UBASE_ERR_NONE;
}

/** @This returns a pointer to the buffer space of a plane.
 *
 * To use this function, @ref ubuf_pic_common_plane must be allocated and
 * correctly filled in.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @param buffer_p reference written with a pointer to buffer space if not NULL
 * @return an error code
 */
int ubuf_pic_common_plane_map(struct ubuf *ubuf, const char *chroma,
                              int hoffset, int voffset, int hsize, int vsize,
                              uint8_t **buffer_p)
{
    assert(chroma != NULL);
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    int plane = ubuf_pic_common_plane(ubuf->mgr, chroma);
    if (unlikely(plane < 0))
        return UBASE_ERR_INVALID;

    size_t hgran = common_mgr->macropixel * common_mgr->planes[plane]->hsub;
    size_t vgran = common_mgr->planes[plane]->vsub;

    /* Check offsets. */
    if (hoffset < 0)
        hoffset = common->hmsize * common_mgr->macropixel + hoffset;
    if (voffset < 0)
        voffset = common->vsize + voffset;
    if (unlikely(hoffset % hgran || voffset % vgran))
        return UBASE_ERR_INVALID;
    int hmoffset = hoffset / common_mgr->macropixel;

    /* Check sizes - we don't actually use them. */
    int hmsize;
    if (hsize < 0)
        hmsize = common->hmsize - hmoffset;
    else if (unlikely(hsize % hgran))
        return UBASE_ERR_INVALID;
    else {
        hmsize = hsize / common_mgr->macropixel;
        if (unlikely(hmsize > common->hmsize - hmoffset))
            return UBASE_ERR_INVALID;
    }
    if (vsize < 0)
        vsize = common->vsize - voffset;
    else if (unlikely(vsize % vgran ||
                      vsize > common->vsize - voffset))
        return UBASE_ERR_INVALID;

    if (likely(buffer_p != NULL))
        *buffer_p = common->planes[plane].buffer +
            (common->vprepend + voffset) / common_mgr->planes[plane]->vsub *
                common->planes[plane].stride +
            (common->hmprepend + hmoffset) / common_mgr->planes[plane]->hsub *
                common_mgr->planes[plane]->macropixel_size;
    return UBASE_ERR_NONE;
}

/** @This checks whether the requested picture resize can be performed with
 * this manager.
 *
 * @param mgr common management structure
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @return an error code
 */
int ubuf_pic_common_check_skip(struct ubuf_mgr *mgr, int hskip, int vskip)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);
    if (unlikely(hskip < 0))
        hskip = -hskip;
    if (unlikely(vskip < 0))
        vskip = -vskip;

    for (uint8_t plane = 0; plane < common_mgr->nb_planes; plane++) {
        size_t hgran = common_mgr->macropixel * common_mgr->planes[plane]->hsub;
        size_t vgran = common_mgr->planes[plane]->vsub;
        if (unlikely(hskip % hgran || vskip % vgran))
            return UBASE_ERR_INVALID;
    }
    return UBASE_ERR_NONE;
}

/** @This splits an interlaced picture ubuf in its two fields.
 *
 * Two extra ubufs are allocated, one per field.
 *
 * @param ubuf pointer to ubuf
 * @param odd pointer to pointer to odd field ubuf
 * @param even pointer to pointer to even field ubuf
 * @return an error code
*/
int ubuf_pic_common_split_fields(struct ubuf *ubuf, struct ubuf **odd,
        struct ubuf **even)
{
    *odd = ubuf_dup(ubuf);
    if (!*odd)
        return UBASE_ERR_ALLOC;

    *even = ubuf_dup(ubuf);
    if (!*odd) {
        ubuf_free(*odd);
        return UBASE_ERR_ALLOC;
    }


    for (int i = 0; i < 2; i++) {
        struct ubuf *field = i ? *odd : *even;
        struct ubuf_pic_common *pic_common = ubuf_pic_common_from_ubuf(field);
        pic_common->vsize /= 2;

        const char *chroma = NULL;
        while (ubase_check(ubuf_pic_iterate_plane(ubuf, &chroma)) && chroma) {
            int plane = ubuf_pic_common_plane(ubuf->mgr, chroma);
            if (plane < 0) {
                abort();
            }

            struct ubuf_pic_common_plane *p = &pic_common->planes[plane];
            size_t stride = p->stride;
            uint8_t *buffer = p->buffer;
            if (i)
                buffer += stride;
            ubuf_pic_common_plane_init(field, plane, buffer, 2 * stride);
        }
    }

    return UBASE_ERR_NONE;
}

/** @This resizes a picture ubuf, if the ubuf has enough space, and it is not
 * shared.
 *
 * @param ubuf pointer to ubuf
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @param new_hsize final horizontal size of the buffer, in pixels (if set
 * to -1, keep same line ends)
 * @param new_vsize final vertical size of the buffer, in lines (if set
 * to -1, keep same last line)
 * @return an error code
 */
int ubuf_pic_common_resize(struct ubuf *ubuf, int hskip, int vskip,
                           int new_hsize, int new_vsize)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    int new_hmsize, hmskip;

    if (unlikely(new_hsize == -1))
        new_hsize = common->hmsize * common_mgr->macropixel - hskip;
    if (unlikely(new_vsize == -1))
        new_vsize = common->vsize - vskip;
    if (unlikely(!hskip && !vskip &&
                 new_hsize == common->hmsize * common_mgr->macropixel &&
                 new_vsize == common->vsize))
        return UBASE_ERR_NONE; /* nothing to do */

    UBASE_RETURN(ubuf_pic_common_check_size(ubuf->mgr, new_hsize, new_vsize));
    UBASE_RETURN(ubuf_pic_common_check_skip(ubuf->mgr, hskip, vskip));

    new_hmsize = new_hsize / common_mgr->macropixel;
    hmskip = hskip / common_mgr->macropixel;

    if (unlikely(hmskip < 0 && new_hmsize < -hmskip))
        return UBASE_ERR_INVALID;
    if (unlikely(vskip < 0 && new_vsize < -vskip))
        return UBASE_ERR_INVALID;
    if (unlikely(hmskip >= 0 && common->hmsize < hmskip))
        return UBASE_ERR_INVALID;
    if (unlikely(vskip >= 0 && common->vsize < vskip))
        return UBASE_ERR_INVALID;

    size_t hmoffset = common->hmprepend;
    size_t hmhigh = common->hmprepend + common->hmsize + common->hmappend;
    size_t voffset = common->vprepend;
    size_t vhigh = common->vprepend + common->vsize + common->vappend;

    if (likely((int)hmoffset + hmskip >= 0 &&
               (int)hmoffset + hmskip + new_hmsize <= hmhigh &&
               (int)voffset + vskip >= 0 &&
               (int)voffset + vskip + new_vsize <= vhigh)) {
        common->hmprepend += hmskip;
        common->hmappend = hmhigh - common->hmprepend - new_hmsize;
        common->hmsize = new_hmsize;
        common->vprepend += vskip;
        common->vappend = vhigh - common->vprepend - new_vsize;
        common->vsize = new_vsize;
        return UBASE_ERR_NONE;
    }

    return UBASE_ERR_INVALID;
}

/** @This frees memory allocated by @ref ubuf_pic_common_mgr_init and
 * @ref ubuf_pic_common_mgr_add_plane.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a ubuf_pic_common_mgr
 */
void ubuf_pic_common_mgr_clean(struct ubuf_mgr *mgr)
{
    struct ubuf_pic_common_mgr *pic_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);

    for (uint8_t plane = 0; plane < pic_mgr->nb_planes; plane++) {
        free(pic_mgr->planes[plane]->chroma);
        free(pic_mgr->planes[plane]);
    }
    free(pic_mgr->planes);
}

/** @This allocates a new instance of the ubuf manager for picture formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a ubuf_pic_common_mgr
 * @param macropixel number of pixels in a macropixel (typically 1)
 */
void ubuf_pic_common_mgr_init(struct ubuf_mgr *mgr, uint8_t macropixel)
{
    assert(macropixel);

    struct ubuf_pic_common_mgr *pic_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);

    pic_mgr->macropixel = macropixel;
    pic_mgr->nb_planes = 0;
    pic_mgr->planes = NULL;
}

/** @This adds a new plane to a ubuf manager for picture formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a ubuf_pic_common_mgr
 * @param chroma chroma type (see chroma reference)
 * @param hsub horizontal subsamping
 * @param vsub vertical subsamping
 * @param macropixel_size size of a macropixel in octets
 * @return an error code
 */
int ubuf_pic_common_mgr_add_plane(struct ubuf_mgr *mgr, const char *chroma,
                                  uint8_t hsub, uint8_t vsub,
                                  uint8_t macropixel_size)
{
    assert(chroma != NULL);
    assert(hsub);
    assert(vsub);
    assert(macropixel_size);
    assert(mgr->refcount == NULL || urefcount_single(mgr->refcount));

    struct ubuf_pic_common_mgr *pic_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);
    struct ubuf_pic_common_mgr_plane *plane =
        malloc(sizeof(struct ubuf_pic_common_mgr_plane));
    if (unlikely(plane == NULL))
        return UBASE_ERR_INVALID;

    plane->chroma = strdup(chroma);
    plane->hsub = hsub;
    plane->vsub = vsub;
    plane->macropixel_size = macropixel_size;

    struct ubuf_pic_common_mgr_plane **planes = realloc(pic_mgr->planes,
        (pic_mgr->nb_planes + 1) * sizeof(struct ubuf_pic_common_mgr_plane *));
    if (unlikely(planes == NULL)) {
        free(plane);
        return UBASE_ERR_ALLOC;
    }
    pic_mgr->planes = planes;
    pic_mgr->planes[pic_mgr->nb_planes++] = plane;
    return UBASE_ERR_NONE;
}

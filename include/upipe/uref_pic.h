/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
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
 * @short Upipe wrapper for picture ubuf and uref
 */

#ifndef _UPIPE_UREF_PIC_H_
/** @hidden */
#define _UPIPE_UREF_PIC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_pic.h"

#include <stdint.h>

UREF_ATTR_UNSIGNED_SH(pic, number, UDICT_TYPE_PIC_NUM, picture number)
UREF_ATTR_VOID_SH(pic, key, UDICT_TYPE_PIC_KEY, key picture)
UREF_ATTR_UNSIGNED_SH(pic, hposition, UDICT_TYPE_PIC_HPOSITION,
        horizontal position)
UREF_ATTR_UNSIGNED_SH(pic, vposition, UDICT_TYPE_PIC_VPOSITION,
        vertical position)
UREF_ATTR_UNSIGNED_SH(pic, lpadding, UDICT_TYPE_PIC_LPADDING, left padding)
UREF_ATTR_UNSIGNED_SH(pic, rpadding, UDICT_TYPE_PIC_RPADDING, right padding)
UREF_ATTR_UNSIGNED_SH(pic, tpadding, UDICT_TYPE_PIC_TPADDING, top padding)
UREF_ATTR_UNSIGNED_SH(pic, bpadding, UDICT_TYPE_PIC_BPADDING, bottom padding)
UREF_ATTR_VOID_SH(pic, progressive, UDICT_TYPE_PIC_PROGRESSIVE, progressive)
UREF_ATTR_VOID_SH(pic, tf, UDICT_TYPE_PIC_TF, top field present)
UREF_ATTR_VOID_SH(pic, bf, UDICT_TYPE_PIC_BF, bottom field present)
UREF_ATTR_VOID_SH(pic, tff, UDICT_TYPE_PIC_TFF, top field first)
UREF_ATTR_SMALL_UNSIGNED_SH(pic, afd, UDICT_TYPE_PIC_AFD, active format description)
UREF_ATTR_OPAQUE_SH(pic, cea_708, UDICT_TYPE_PIC_CEA_708, cea-708 captions)
UREF_ATTR_OPAQUE_SH(pic, bar_data, UDICT_TYPE_PIC_BAR_DATA, afd bar data)
UREF_ATTR_OPAQUE_SH(pic, s12m, UDICT_TYPE_PIC_S12M, SMPTE 12M timecode compatible with ffmpeg AV_FRAME_DATA_S12M_TIMECODE)
UREF_ATTR_UNSIGNED(pic, original_height, "p.original_height", original picture height before chunking)
UREF_ATTR_VOID(pic, c_not_y, "p.c_not_y", whether ancillary data is found in chroma space)

/** @This returns a new uref pointing to a new ubuf pointing to a picture.
 * This is equivalent to the two operations sequentially, and is a shortcut.
 *
 * @param uref_mgr management structure for this uref type
 * @param ubuf_mgr management structure for this ubuf type
 * @param hsize horizontal size in pixels
 * @param vsize vertical size in lines
 * @return pointer to uref or NULL in case of failure
 */
static inline struct uref *uref_pic_alloc(struct uref_mgr *uref_mgr,
                                          struct ubuf_mgr *ubuf_mgr,
                                          int hsize, int vsize)
{
    struct uref *uref = uref_alloc(uref_mgr);
    if (unlikely(uref == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_pic_alloc(ubuf_mgr, hsize, vsize);
    if (unlikely(ubuf == NULL)) {
        uref_free(uref);
        return NULL;
    }

    uref_attach_ubuf(uref, ubuf);
    return uref;
}

/** @see ubuf_pic_size */
static inline int uref_pic_size(struct uref *uref,
                                size_t *hsize_p, size_t *vsize_p,
                                uint8_t *macropixel_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_size(uref->ubuf, hsize_p, vsize_p, macropixel_p);
}

/** @see ubuf_pic_iterate_plane */
static inline int uref_pic_iterate_plane(struct uref *uref,
                                         const char **chroma_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_iterate_plane(uref->ubuf, chroma_p);
}

/** DO NOT USE: deprecated, use uref_pic_iterate_plane instead  */
static inline UBASE_DEPRECATED
int uref_pic_plane_iterate(struct uref *uref,
                           const char **chroma_p)
{
    return uref_pic_iterate_plane(uref, chroma_p);
}

/** helper for uref_pic_iterate_plane */
#define uref_pic_foreach_plane(UREF, CHROMA)                                \
    for (CHROMA = NULL;                                                     \
         ubase_check(uref_pic_iterate_plane(UREF, &CHROMA)) && CHROMA != NULL;)

/** @see ubuf_pic_plane_size */
static inline int uref_pic_plane_size(struct uref *uref,
        const char *chroma, size_t *stride_p, uint8_t *hsub_p, uint8_t *vsub_p,
        uint8_t *macropixel_size_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_plane_size(uref->ubuf, chroma, stride_p, hsub_p, vsub_p,
                               macropixel_size_p);
}

/** @see ubuf_pic_plane_read */
static inline int uref_pic_plane_read(struct uref *uref,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize,
        const uint8_t **buffer_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_plane_read(uref->ubuf, chroma, hoffset, voffset,
                               hsize, vsize, buffer_p);
}

/** @see ubuf_pic_plane_write */
static inline int uref_pic_plane_write(struct uref *uref,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize,
        uint8_t **buffer_p)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_plane_write(uref->ubuf, chroma, hoffset, voffset,
                                hsize, vsize, buffer_p);
}

/** @see ubuf_pic_plane_unmap */
static inline int uref_pic_plane_unmap(struct uref *uref,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_plane_unmap(uref->ubuf, chroma, hoffset, voffset,
                                hsize, vsize);
}

/** @see ubuf_pic_plane_clear */
static inline int uref_pic_plane_clear(struct uref *uref,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize,
        int fullrange)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_plane_clear(uref->ubuf, chroma, hoffset, voffset,
                                hsize, vsize, fullrange);
}

/** @see ubuf_split_fields */
static inline int uref_split_fields(struct uref *uref, struct uref **odd,
        struct uref **even)
{
    int ret = UBASE_ERR_ALLOC;

    *odd = uref_dup_inner(uref);
    *even = uref_dup_inner(uref);

    if (*odd && *even)
        ret = ubuf_split_fields(uref->ubuf, &(*odd)->ubuf, &(*even)->ubuf);

    if (!ubase_check(ret)) {
        uref_free(*odd);
        uref_free(*even);
    }

    return ret;
}

/** @see ubuf_pic_resize */
static inline int uref_pic_resize(struct uref *uref,
        int hskip, int vskip, int new_hsize, int new_vsize)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_resize(uref->ubuf, hskip, vskip, new_hsize, new_vsize);
}

/** @see ubuf_pic_clear */
static inline int uref_pic_clear(struct uref *uref,
        int hoffset, int voffset, int hsize, int vsize,
        int fullrange)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_clear(uref->ubuf, hoffset, voffset, hsize, vsize, fullrange);
}

/** @see ubuf_pic_blit */
static inline int uref_pic_blit(struct uref *uref, struct ubuf *ubuf,
                                int dest_hoffset, int dest_voffset,
                                int src_hoffset, int src_voffset,
                                int extract_hsize, int extract_vsize,
                                const int alpha, const int threshold)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_blit(uref->ubuf, ubuf, dest_hoffset, dest_voffset,
                         src_hoffset, src_voffset,
                         extract_hsize, extract_vsize, alpha, threshold);
}

/** @This allocates a new ubuf of size new_hsize/new_vsize, and copies part of
 * the old picture ubuf to the new one, switches the ubufs and frees
 * the old one.
 *
 * @param uref pointer to uref structure
 * @param ubuf_mgr management structure for the new ubuf
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
static inline int uref_pic_replace(struct uref *uref,
        struct ubuf_mgr *ubuf_mgr, int hskip, int vskip,
        int new_hsize, int new_vsize)
{
    if (uref->ubuf == NULL)
        return UBASE_ERR_INVALID;
    return ubuf_pic_replace(ubuf_mgr, &uref->ubuf, hskip, vskip,
                            new_hsize, new_vsize);
}

#ifdef __cplusplus
}
#endif
#endif

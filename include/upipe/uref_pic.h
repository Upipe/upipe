/*****************************************************************************
 * uref_pic.h: picture semantics for uref and ubuf structures
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

#ifndef _UPIPE_UREF_PIC_H_
/** @hidden */
#define _UPIPE_UREF_PIC_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_pic_flow.h>

#include <stdint.h>
#include <assert.h>

UREF_ATTR_TEMPLATE(pic, hoffset, "p.hoffset", unsigned, uint64_t, horizontal offset)
UREF_ATTR_TEMPLATE(pic, voffset, "p.voffset", unsigned, uint64_t, vertical offset)
UREF_ATTR_TEMPLATE(pic, hsize, "p.hsize", unsigned, uint64_t, horizontal size)
UREF_ATTR_TEMPLATE(pic, vsize, "p.vsize", unsigned, uint64_t, vertical size)
UREF_ATTR_TEMPLATE(pic, hposition, "p.hposition", unsigned, uint64_t, horizontal position)
UREF_ATTR_TEMPLATE(pic, vposition, "p.vposition", unsigned, uint64_t, vertical position)
UREF_ATTR_TEMPLATE(pic, aspect, "p.aspect", rational, struct urational, aspect ratio)
UREF_ATTR_TEMPLATE_VOID(pic, interlaced, "p.interlaced", interlaced)
UREF_ATTR_TEMPLATE_VOID(pic, tff, "p.tff", top field first)
UREF_ATTR_TEMPLATE(pic, fields, "p.fields", small_unsigned, uint8_t, number of fields)

/** @This returns a new uref pointing to a new ubuf pointing to a picture.
 * It also sets the required uref attributes.
 *
 * @param uref_mgr management structure for this uref pool
 * @param ubuf_mgr management structure for this ubuf pool
 * @param size size of the buffer, or -1
 * @param prepend extra space before buffer, or -1
 * @param append extra space after buffer, or -1
 * @param align alignment of buffer, or -1
 * @return pointer to uref or NULL in case of failure
 */
static inline struct uref *uref_pic_alloc(struct uref_mgr *uref_mgr,
                                          struct ubuf_mgr *ubuf_mgr,
                                          int hsize, int vsize)
{
    struct uref *uref = uref_ubuf_alloc(uref_mgr, ubuf_mgr,
                                        UBUF_ALLOC_TYPE_PICTURE, hsize, vsize);
    if (unlikely(uref == NULL)) return NULL;

    if (unlikely(!uref_pic_set_hsize(&uref, hsize) ||
                 !uref_pic_set_vsize(&uref, vsize))) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

/** @This returns the size of a picture.
 *
 * @param uref uref structure
 * @param hsize_p reference written with horizontal size, in pixels
 * @param vsize_p reference written with vertical size, in lines
 * @return pointer to plane or NULL in case of error
 */
static inline void uref_pic_size(struct uref *uref, size_t *hsize_p,
                                 size_t *vsize_p)
{
    if (likely(hsize_p != NULL)) {
        uint64_t hsize = 0;
        uref_pic_get_hsize(uref, &hsize);
        *hsize_p = hsize;
    }
    if (likely(vsize_p != NULL)) {
        uint64_t vsize = 0;
        uref_pic_get_vsize(uref, &vsize);
        *vsize_p = vsize;
    }
}

/** @internal @This returns a pointer to a plane.
 *
 * @param uref uref structure
 * @param pic_flow picture flow definition packet
 * @param plane plane number
 * @param stride_p distance in octets between lines in the buffer
 * @return pointer to plane or NULL in case of error
 */
static inline uint8_t *uref_pic_plane(struct uref *uref, struct uref *pic_flow,
                                      uint8_t plane, size_t *stride_p)
{
    if (unlikely(uref->ubuf == NULL)) {
        if (likely(stride_p != NULL))
            *stride_p = 0;
        return NULL;
    }

    uint64_t hoffset = 0, voffset = 0;
    uint8_t macropixel, macropixel_size, hsub, vsub;
    uref_pic_get_hoffset(uref, &hoffset);
    uref_pic_get_voffset(uref, &voffset);

    if (unlikely(!uref_pic_flow_get_macropixel(pic_flow, &macropixel) ||
                 !uref_pic_flow_get_hsubsampling(pic_flow, &hsub, plane) ||
                 !uref_pic_flow_get_vsubsampling(pic_flow, &vsub, plane) ||
                 !uref_pic_flow_get_macropixel_size(pic_flow, &macropixel_size,
                                                    plane)))
        return false;

    if (likely(stride_p != NULL))
        *stride_p = uref->ubuf->planes[plane].stride;
    return uref->ubuf->planes[plane].buffer +
           voffset * uref->ubuf->planes[plane].stride / vsub +
           hoffset / macropixel * macropixel_size / hsub;
}

/** @This returns a pointer to a plane defined by its chroma.
 *
 * @param uref struct uref structure
 * @param pic_flow picture flow definition packet
 * @param stride_p distance in octets between lines in the buffer
 * @return pointer to plane or NULL in case of error
 */
static inline uint8_t *uref_pic_chroma(struct uref *uref, struct uref *pic_flow,
                                       const char *chroma, size_t *stride_p)
{
    uint8_t plane;
    if (unlikely(!uref_pic_flow_find_chroma(pic_flow, chroma, &plane)))
        return NULL;
    return uref_pic_plane(uref, pic_flow, plane, stride_p);
}

/** @This resizes the buffer space pointed to by an struct uref, in an efficient
 * manner.
 *
 * @param uref_p reference to a struct uref structure
 * @param ubuf_mgr ubuf management structure in case duplication is needed
 * (may be NULL if the resize is only a space reduction)
 * @param pic_flow picture flow definition packet
 * @param new_hsize final horizontal size of the buffer, in pixels (if set
 * to -1, keep same line ends)
 * @param new_vsize final vertical size of the buffer, in lines (if set
 * to -1, keep same last line)
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @return true if the operation succeeded
 */
static inline bool uref_pic_resize(struct uref **uref_p,
                                   struct ubuf_mgr *ubuf_mgr,
                                   struct uref *pic_flow,
                                   int new_hsize, int new_vsize,
                                   int hskip, int vskip)
{
    struct uref *uref = *uref_p;
    assert(uref->ubuf != NULL);
    bool ret;
    uint64_t hoffset = 0, voffset = 0;
    uint64_t hsize = 0, vsize = 0;
    uint64_t max_hsize, max_vsize;
    uint8_t macropixel, planes;
    uref_pic_get_hoffset(uref, &hoffset);
    uref_pic_get_voffset(uref, &voffset);
    uref_pic_get_hsize(uref, &hsize);
    uref_pic_get_vsize(uref, &vsize);
    max_hsize = hsize + hoffset;
    max_vsize = vsize + voffset;
    if (unlikely(new_hsize == -1))
        new_hsize = hsize - hskip;
    if (unlikely(new_vsize == -1))
        new_vsize = vsize - vskip;

    if (unlikely(!uref_pic_flow_get_macropixel(pic_flow, &macropixel) ||
                 !uref_pic_flow_get_planes(pic_flow, &planes)))
        return false;

    for (uint8_t plane = 0; plane < planes; plane++) {
        uint8_t hsub, vsub;
        size_t hgran, vgran;
        if (unlikely(!uref_pic_flow_get_hsubsampling(pic_flow, &hsub, plane) ||
                     !uref_pic_flow_get_vsubsampling(pic_flow, &vsub, plane)))
            return false;
        hgran = macropixel * hsub;
        vgran = vsub;
        if (unlikely(hskip % hgran || vskip % vgran))
            return false;
    }

    if (unlikely(hskip < 0 && new_hsize < -hskip)) return false;
    if (unlikely(vskip < 0 && new_vsize < -vskip)) return false;
    if (unlikely(hskip >= 0 && hsize < hskip)) return false;
    if (unlikely(vskip >= 0 && vsize < vskip)) return false;

    /* if the buffer is not shared, the manager implementation is faster */
    if (likely(ubuf_single(uref->ubuf)))
        goto uref_pic_resize_ubuf;

    /* try just changing the attributes */
    if (likely((int64_t)hoffset + hskip >= 0 &&
               (int64_t)hoffset + hskip + new_hsize <= max_hsize &&
               (int64_t)voffset + vskip >= 0 &&
               (int64_t)voffset + vskip + new_vsize <= max_vsize)) {
        if (likely(hskip != 0))
            if (unlikely(!uref_pic_set_hoffset(uref_p, hoffset + hskip)))
                return false;
        if (likely(vskip != 0))
            if (unlikely(!uref_pic_set_voffset(uref_p, voffset + vskip)))
                return false;
        if (likely(new_hsize != hsize))
            if (unlikely(!uref_pic_set_hsize(uref_p, new_hsize)))
                return false;
        if (likely(new_vsize != vsize))
            if (unlikely(!uref_pic_set_vsize(uref_p, new_vsize)))
                return false;
        return true;
    }

    /* we'll have to change the ubuf */
    assert(ubuf_mgr != NULL);

uref_pic_resize_ubuf:
    ret = ubuf_pic_resize(ubuf_mgr, &uref->ubuf, new_hsize, new_vsize,
                          hoffset + hskip, voffset + vskip);
    if (likely(ret)) {
        if (unlikely(!uref_pic_set_hsize(uref_p, new_hsize)))
            return false;
        if (unlikely(!uref_pic_set_vsize(uref_p, new_vsize)))
            return false;
        uref_pic_delete_hoffset(*uref_p);
        uref_pic_delete_voffset(*uref_p);
    }
    return ret;
}

#define uref_pic_release uref_release
#define uref_pic_writable uref_ubuf_writable
#define uref_pic_dup uref_dup

#endif

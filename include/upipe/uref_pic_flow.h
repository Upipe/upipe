/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe picture flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_PIC_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_PIC_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

/** @internal flow definition prefix for pic allocator */
#define UREF_PIC_FLOW_DEF "pic."

/* The following attributes define an ubuf picture manager format. */
UREF_ATTR_SMALL_UNSIGNED(pic_flow, macropixel, "p.macropixel",
        number of pixels in a macropixel)
UREF_ATTR_SMALL_UNSIGNED(pic_flow, planes, "p.planes",
        number of planes)
UREF_ATTR_SMALL_UNSIGNED_VA(pic_flow, hsubsampling, "p.hsub[%"PRIu8"]",
        horizontal subsampling, uint8_t plane, plane)
UREF_ATTR_SMALL_UNSIGNED_VA(pic_flow, vsubsampling, "p.vsub[%"PRIu8"]",
        vertical subsampling, uint8_t plane, plane)
UREF_ATTR_SMALL_UNSIGNED_VA(pic_flow, macropixel_size, "p.macropix[%"PRIu8"]",
        size of a compound, uint8_t plane, plane)
UREF_ATTR_STRING_VA(pic_flow, chroma, "p.chroma[%"PRIu8"]",
        chroma type, uint8_t plane, plane)

UREF_ATTR_RATIONAL(pic_flow, fps, "p.fps", frames per second)
UREF_ATTR_SMALL_UNSIGNED(pic_flow, hmprepend, "p.hmprepend",
        extra macropixels added before each line)
UREF_ATTR_SMALL_UNSIGNED(pic_flow, hmappend, "p.hmappend",
        extra macropixels added after each line)
UREF_ATTR_SMALL_UNSIGNED(pic_flow, vprepend, "p.vprepend",
        extra lines added before buffer)
UREF_ATTR_SMALL_UNSIGNED(pic_flow, vappend, "p.vappend",
        extra lines added after buffer)
UREF_ATTR_UNSIGNED(pic_flow, align, "p.align", alignment in octets)
UREF_ATTR_INT(pic_flow, align_hmoffset, "p.align_hmoffset",
        horizontal offset of the aligned macropixel)

UREF_ATTR_RATIONAL_SH(pic_flow, sar, UDICT_TYPE_PIC_SAR, sample aspect ratio)
UREF_ATTR_VOID_SH(pic_flow, overscan, UDICT_TYPE_PIC_OVERSCAN, overscan)
UREF_ATTR_RATIONAL(pic_flow, dar, "p.dar", display aspect ratio)
UREF_ATTR_UNSIGNED_SH(pic_flow, hsize, UDICT_TYPE_PIC_HSIZE, horizontal size)
UREF_ATTR_UNSIGNED_SH(pic_flow, vsize, UDICT_TYPE_PIC_VSIZE, vertical size)
UREF_ATTR_UNSIGNED_SH(pic_flow, hsize_visible, UDICT_TYPE_PIC_HSIZE_VISIBLE,
        horizontal visible size)
UREF_ATTR_UNSIGNED_SH(pic_flow, vsize_visible, UDICT_TYPE_PIC_VSIZE_VISIBLE,
        vertical visible size)
UREF_ATTR_STRING_SH(pic_flow, video_format, UDICT_TYPE_PIC_VIDEO_FORMAT,
        video format)
UREF_ATTR_VOID_SH(pic_flow, full_range, UDICT_TYPE_PIC_FULL_RANGE,
        colour full range)
UREF_ATTR_STRING_SH(pic_flow, colour_primaries, UDICT_TYPE_PIC_COLOUR_PRIMARIES,
        colour primaries)
UREF_ATTR_STRING_SH(pic_flow, transfer_characteristics,
        UDICT_TYPE_PIC_TRANSFER_CHARACTERISTICS, transfer characteristics)
UREF_ATTR_STRING_SH(pic_flow, matrix_coefficients,
        UDICT_TYPE_PIC_MATRIX_COEFFICIENTS, matrix coefficients)

/** @This allocates a control packet to define a new picture flow. For each
 * plane, uref_pic_flow_add_plane() has to be called afterwards.
 *
 * @param mgr uref management structure
 * @param macropixel number of pixels in a macropixel
 * @param planes number of planes
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_pic_flow_alloc_def(struct uref_mgr *mgr,
                                                   uint8_t macropixel)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL))
        return NULL;
    if (unlikely(!(ubase_check(uref_flow_set_def(uref, UREF_PIC_FLOW_DEF)) &&
                   ubase_check(uref_pic_flow_set_macropixel(uref, macropixel)) &&
                   ubase_check(uref_pic_flow_set_planes(uref, 0))))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

/** @This registers a new plane in the picture flow definition packet.
 *
 * @param uref uref control packet
 * @param hsub horizontal subsampling
 * @param vsub vertical subsampling
 * @param macropixel_size size in octets of a compound
 * @param chroma chroma type (see chroma reference)
 * @return an error code
 */
static inline int uref_pic_flow_add_plane(struct uref *uref,
           uint8_t hsub, uint8_t vsub, uint8_t macropixel_size,
           const char *chroma)
{
    uint8_t plane = 0;
    if (unlikely(hsub == 0 || vsub == 0 || macropixel_size == 0 ||
                 chroma == NULL))
        return UBASE_ERR_INVALID;
    uref_pic_flow_get_planes(uref, &plane);
    UBASE_RETURN(uref_pic_flow_set_planes(uref, plane + 1))
    UBASE_RETURN(uref_pic_flow_set_hsubsampling(uref, hsub, plane))
    UBASE_RETURN(uref_pic_flow_set_vsubsampling(uref, vsub, plane))
    UBASE_RETURN(uref_pic_flow_set_macropixel_size(uref, macropixel_size, plane))
    UBASE_RETURN(uref_pic_flow_set_chroma(uref, chroma, plane))
    return UBASE_ERR_NONE;
}

/** @internal @This finds a plane by its chroma.
 *
 * @param uref uref control packet
 * @param chroma chroma type
 * @param plane_p written with the matching plane number
 * @return an error code
 */
static inline int uref_pic_flow_find_chroma(struct uref *uref,
                                            const char *chroma,
                                            uint8_t *plane_p)
{
    assert(chroma != NULL);
    uint8_t planes = 0;
    uref_pic_flow_get_planes(uref, &planes);

    for (uint8_t plane = 0; plane < planes; plane++) {
        const char *plane_chroma;
        UBASE_RETURN(uref_pic_flow_get_chroma(uref, &plane_chroma, plane))
        if (unlikely(!strcmp(chroma, plane_chroma))) {
            *plane_p = plane;
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_INVALID;
}

/** @This checks if there is a plane with the given properties.
 *
 * @param uref uref control packet
 * @param hsub horizontal subsampling
 * @param vsub vertical subsampling
 * @param mpixel_size size in octets of a compound
 * @param chroma chroma type
 * @return an error code
 */
static inline int uref_pic_flow_check_chroma(struct uref *uref,
          uint8_t hsub, uint8_t vsub, uint8_t mpixel_size,
          const char *chroma)
{
    uint8_t plane, hsub2, vsub2, mpixel_size2;
    UBASE_RETURN(uref_pic_flow_find_chroma(uref, chroma, &plane))
    UBASE_RETURN(uref_pic_flow_get_hsubsampling(uref, &hsub2, plane))
    UBASE_RETURN(uref_pic_flow_get_vsubsampling(uref, &vsub2, plane))
    UBASE_RETURN(uref_pic_flow_get_macropixel_size(uref, &mpixel_size2, plane))
    return (hsub2 == hsub && vsub2 == vsub && mpixel_size2 == mpixel_size) ?
           UBASE_ERR_NONE : UBASE_ERR_INVALID;
}

/** @This copies the attributes defining the ubuf manager format to
 * another uref.
 *
 * @param uref_dst destination uref
 * @param uref_src source uref
 * @return an error code
 */
static inline int uref_pic_flow_copy_format(struct uref *uref_dst,
                                            struct uref *uref_src)
{
    const char *def;
    uint8_t planes, macropixel;
    UBASE_RETURN(uref_flow_get_def(uref_src, &def))
    UBASE_RETURN(uref_flow_set_def(uref_dst, def))
    UBASE_RETURN(uref_pic_flow_get_macropixel(uref_src, &macropixel))
    UBASE_RETURN(uref_pic_flow_set_macropixel(uref_dst, macropixel))
    UBASE_RETURN(uref_pic_flow_get_planes(uref_src, &planes))
    UBASE_RETURN(uref_pic_flow_set_planes(uref_dst, planes))

    for (uint8_t plane = 0; plane < planes; plane++) {
        const char *chroma;
        uint8_t var;
        UBASE_RETURN(uref_pic_flow_get_chroma(uref_src, &chroma, plane))
        UBASE_RETURN(uref_pic_flow_set_chroma(uref_dst, chroma, plane))
        UBASE_RETURN(uref_pic_flow_get_hsubsampling(uref_src, &var, plane))
        UBASE_RETURN(uref_pic_flow_set_hsubsampling(uref_dst, var, plane))
        UBASE_RETURN(uref_pic_flow_get_vsubsampling(uref_src, &var, plane))
        UBASE_RETURN(uref_pic_flow_set_vsubsampling(uref_dst, var, plane))
        UBASE_RETURN(uref_pic_flow_get_macropixel_size(uref_src, &var,
                                                        plane))
        UBASE_RETURN(uref_pic_flow_set_macropixel_size(uref_dst, var, plane))
    }
    return UBASE_ERR_NONE;
}

/** @This iterates on chroma plane, and returns the highest horizontal and
 * vertical subsampling.
 *
 * @param uref uref control packet
 * @param hsub_p filled with the highest horizontal subsampling
 * @param vsub_p filled with the highest vertical subsampling
 * @return an error code
 */
static inline int uref_pic_flow_max_subsampling(struct uref *uref,
        uint8_t *hsub_p, uint8_t *vsub_p)
{
    uint8_t planes;
    UBASE_RETURN(uref_pic_flow_get_planes(uref, &planes))
    *hsub_p = *vsub_p = 1;

    for (uint8_t plane = 0; plane < planes; plane++) {
        uint8_t var;
        UBASE_RETURN(uref_pic_flow_get_hsubsampling(uref, &var, plane))
        if (var > *hsub_p)
            *hsub_p = var;
        UBASE_RETURN(uref_pic_flow_get_vsubsampling(uref, &var, plane))
        if (var > *vsub_p)
            *vsub_p = var;
    }
    return UBASE_ERR_NONE;
}

/** @This clears the attributes defining the ubuf_pic manager format.
 *
 * @param uref uref control packet
 */
static inline void uref_pic_flow_clear_format(struct uref *uref)
{
    uref_pic_flow_delete_macropixel(uref);

    uint8_t planes;
    if (unlikely(!ubase_check(uref_pic_flow_get_planes(uref, &planes))))
        return;

    for (uint8_t plane = 0; plane < planes; plane++) {
        uref_pic_flow_delete_chroma(uref, plane);
        uref_pic_flow_delete_hsubsampling(uref, plane);
        uref_pic_flow_delete_vsubsampling(uref, plane);
        uref_pic_flow_delete_macropixel_size(uref, plane);
    }
    uref_pic_flow_delete_planes(uref);
}


/** @This compares the format flow definition between two urefs.
 *
 * @param uref1 first uref
 * @param uref2 second uref
 * @return true if both urefs describe the same format
 */
static inline bool uref_pic_flow_compare_format(struct uref *uref1,
                                                struct uref *uref2)
{
    if (uref_flow_cmp_def(uref1, uref2) != 0 ||
        uref_pic_flow_cmp_macropixel(uref1, uref2) != 0 ||
        uref_pic_flow_cmp_planes(uref1, uref2) != 0)
        return false;

    uint8_t planes;
    UBASE_RETURN(uref_pic_flow_get_planes(uref1, &planes))
    for (uint8_t plane = 0; plane < planes; plane++) {
        if (uref_pic_flow_cmp_chroma(uref1, uref2, plane) != 0 ||
            uref_pic_flow_cmp_hsubsampling(uref1, uref2, plane) != 0 ||
            uref_pic_flow_cmp_vsubsampling(uref1, uref2, plane) != 0 ||
            uref_pic_flow_cmp_macropixel_size(uref1, uref2, plane) != 0)
            return false;
    }
    return true;
}

/** @This infers the SAR from the DAR.
 *
 * @param uref uref control packet
 * @param dar display aspect ratio
 * @return an error code
 */
static inline int uref_pic_flow_infer_sar(struct uref *uref,
                                          struct urational dar)
{
    uint64_t width, height;
    UBASE_RETURN(uref_pic_flow_get_hsize(uref, &width))
    UBASE_RETURN(uref_pic_flow_get_vsize(uref, &height))
    bool overscan = ubase_check(uref_pic_flow_get_overscan(uref));

    struct urational sar;
    sar.num = height * dar.num;
    sar.den = width * dar.den;
    if (overscan) {
        if (width == 720 && height == 576 &&
            dar.num == 4 && dar.den == 3) {
            sar.num = 12;
            sar.den = 11;
        } else if (width == 720 && height == 480 &&
                   dar.num == 4 && dar.den == 3) {
            sar.num = 10;
            sar.den = 11;
        } else if (width == 720 && height == 576 &&
                   dar.num == 16 && dar.den == 9) {
            sar.num = 16;
            sar.den = 11;
        } else if (width == 720 && height == 480 &&
                   dar.num == 16 && dar.den == 9) {
            sar.num = 40;
            sar.den = 33;
        } else if (width == 480 && height == 576 &&
                   dar.num == 16 && dar.den == 9) {
            sar.num = 24;
            sar.den = 11;
        } else if (width == 480 && height == 480 &&
                   dar.num == 16 && dar.den == 9) {
            sar.num = 20;
            sar.den = 11;
        } else if (width == 480 && height == 576 &&
                   dar.num == 4 && dar.den == 3) {
            sar.num = 18;
            sar.den = 11;
        } else if (width == 480 && height == 480 &&
                   dar.num == 4 && dar.den == 3) {
            sar.num = 15;
            sar.den = 11;
        }
    }
    urational_simplify(&sar);
    return uref_pic_flow_set_sar(uref, sar);
}

/** @This infers the DAR from the SAR and overscan in the uref.
 *
 * @param uref uref control packet
 * @param dar_p filled in with the calculated display aspect ratio
 * @return an error code
 */
static inline int uref_pic_flow_infer_dar(struct uref *uref,
                                          struct urational *dar_p)
{
    uint64_t width, height;
    UBASE_RETURN(uref_pic_flow_get_hsize(uref, &width))
    UBASE_RETURN(uref_pic_flow_get_vsize(uref, &height))
    struct urational sar;
    UBASE_RETURN(uref_pic_flow_get_sar(uref, &sar))

    if (ubase_check(uref_pic_flow_get_overscan(uref))) {
        if ((width == 720 && height == 576 &&
             sar.num == 12 && sar.den == 11) ||
            (width == 720 && height == 480 &&
             sar.num == 10 && sar.den == 11) ||
            (width == 480 && height == 576 &&
             sar.num == 18 && sar.den == 11) ||
            (width == 480 && height == 480 &&
             sar.num == 15 && sar.den == 11)) {
            dar_p->num = 4;
            dar_p->den = 3;
            return UBASE_ERR_NONE;
        }
        if ((width == 720 && height == 576 &&
             sar.num == 16 && sar.den == 11) ||
            (width == 720 && height == 480 &&
             sar.num == 40 && sar.den == 33) ||
            (width == 480 && height == 576 &&
             sar.num == 24 && sar.den == 11) ||
            (width == 480 && height == 480 &&
             sar.num == 20 && sar.den == 11)) {
            dar_p->num = 16;
            dar_p->den = 9;
            return UBASE_ERR_NONE;
        }
    }

    dar_p->num = sar.num * width;
    dar_p->den = sar.den * height;
    urational_simplify(dar_p);
    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

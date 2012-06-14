/*****************************************************************************
 * uref_pic_flow.h: picture flow definition attributes for uref
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

#ifndef _UPIPE_UREF_PIC_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_PIC_FLOW_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>

/** @internal flow definition prefix for pic allocator */
#define UREF_PIC_FLOW_DEFINITION "pic."

UREF_ATTR_TEMPLATE(pic_flow, macropixel, "p.macropixel", small_unsigned, uint8_t, number of pixels in a macropixel)
UREF_ATTR_TEMPLATE(pic_flow, planes, "p.planes", small_unsigned, uint8_t, number of planes)
UREF_ATTR_TEMPLATE_VA(pic_flow, hsubsampling, "p.hsub[%"PRIu8"]", small_unsigned, uint8_t, horizontal subsampling, uint8_t plane, plane)
UREF_ATTR_TEMPLATE_VA(pic_flow, vsubsampling, "p.vsub[%"PRIu8"]", small_unsigned, uint8_t, vertical subsampling, uint8_t plane, plane)
UREF_ATTR_TEMPLATE_VA(pic_flow, macropixel_size, "p.macropix[%"PRIu8"]", small_unsigned, uint8_t, size of a compound, uint8_t plane, plane)
UREF_ATTR_TEMPLATE_VA(pic_flow, chroma, "p.chroma[%"PRIu8"]", string, const char *, chroma type, uint8_t plane, plane)
UREF_ATTR_TEMPLATE(pic_flow, fps, "p.fps", float, double, frames per second)
UREF_ATTR_TEMPLATE(pic_flow, hmprepend, "p.hmprepend", small_unsigned, uint8_t, extra macropixels added before each line)
UREF_ATTR_TEMPLATE(pic_flow, hmappend, "p.hmappend", small_unsigned, uint8_t, extra macropixels added after each line)
UREF_ATTR_TEMPLATE(pic_flow, vprepend, "p.vprepend", small_unsigned, uint8_t, extra lines added before buffer)
UREF_ATTR_TEMPLATE(pic_flow, vappend, "p.vappend", small_unsigned, uint8_t, extra lines added after buffer)
UREF_ATTR_TEMPLATE(pic_flow, align, "p.align", unsigned, uint64_t, alignment in octets)
UREF_ATTR_TEMPLATE(pic_flow, align_hmoffset, "p.align_hmoffset", int, int64_t, horizontal offset of the aligned macropixel)
/* hsize and vsize may also be specified in a flow definition packet when
 * the flow has fixed picture size */

/** @This allocates a control packet to define a new picture flow. For each
 * plane, uref_pic_flow_add_plane() has to be called afterwards.
 *
 * @param mgr uref management structure
 * @param macropixel number of pixels in a macropixel
 * @param planes number of planes
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_pic_flow_alloc_definition(struct uref_mgr *mgr,
                                                          uint8_t macropixel)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL)) return NULL;
    if (unlikely(!(uref_flow_set_definition(&uref, UREF_PIC_FLOW_DEFINITION) &&
                   uref_pic_flow_set_macropixel(&uref, macropixel) &&
                   uref_pic_flow_set_planes(&uref, 0)))) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

/** @This registers a new plane in the picture flow definition packet.
 *
 * @param uref_p reference to the uref control packet
 * @param hsub horizontal subsampling
 * @param vsub vertical subsampling
 * @param macropixel_size size in octets of a compound
 * @param chroma chroma type (see chroma reference)
 * @return false in case of error
 */
static inline bool uref_pic_flow_add_plane(struct uref **uref_p,
                                           uint8_t hsub, uint8_t vsub,
                                           uint8_t macropixel_size,
                                           const char *chroma)
{
    uint8_t plane;
    if (unlikely(hsub == 0 || vsub == 0 || macropixel_size == 0 ||
                 chroma == NULL))
        return false;
    if (unlikely(!uref_pic_flow_get_planes(*uref_p, &plane)))
        return false;

    return uref_pic_flow_set_planes(uref_p, plane + 1) &&
           uref_pic_flow_set_hsubsampling(uref_p, hsub, plane) &&
           uref_pic_flow_set_vsubsampling(uref_p, vsub, plane) &&
           uref_pic_flow_set_macropixel_size(uref_p, macropixel_size, plane) &&
           uref_pic_flow_set_chroma(uref_p, chroma, plane);
}

/** @internal @This finds a plane by its chroma.
 *
 * @param uref uref control packet
 * @param chroma chroma type
 * @param plane_p written with the matching plane number
 * @return false in case of error
 */
static inline bool uref_pic_flow_find_chroma(struct uref *uref,
                                             const char *chroma,
                                             uint8_t *plane_p)
{
    assert(chroma != NULL);
    uint8_t planes;
    if (unlikely(!uref_pic_flow_get_planes(uref, &planes)))
        return false;

    for (uint8_t plane = 0; plane < planes; plane++) {
        const char *plane_chroma;
        if (unlikely(!uref_pic_flow_get_chroma(uref, &plane_chroma, plane)))
            return false;
        if (unlikely(!strcmp(chroma, plane_chroma))) {
            *plane_p = plane;
            return true;
        }
    }
    return false;
}

#undef UREF_PIC_FLOW_DEFINITION

#endif

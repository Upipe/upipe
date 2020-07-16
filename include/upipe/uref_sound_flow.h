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

/** @file
 * @short Upipe sound flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_SOUND_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_SOUND_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

#include <stdint.h>
#include <stdbool.h>

/** @internal flow definition prefix for sound allocator */
#define UREF_SOUND_FLOW_DEF "sound."

UREF_ATTR_SMALL_UNSIGNED(sound_flow, planes, "s.planes", number of planes)
UREF_ATTR_STRING_VA(sound_flow, channel, "s.channel[%" PRIu8"]",
        channel type, uint8_t plane, plane)
UREF_ATTR_SMALL_UNSIGNED(sound_flow, channels, "s.channels", number of channels)
UREF_ATTR_SMALL_UNSIGNED(sound_flow, sample_size, "s.sample_size",
        size in octets of a sample of an audio plane)
UREF_ATTR_SMALL_UNSIGNED(sound_flow, raw_sample_size, "s.sample_bits",
        size in bits of an audio sample)
UREF_ATTR_UNSIGNED(sound_flow, rate, "s.rate", samples per second)
UREF_ATTR_UNSIGNED(sound_flow, samples, "s.samples", number of samples)
UREF_ATTR_UNSIGNED(sound_flow, align, "s.align", alignment in octets)
UREF_ATTR_SMALL_UNSIGNED(sound_flow, channel_idx, "s.channel_index", index of first channel)

/** @This allocates a control packet to define a new sound flow.
 *
 * @param mgr uref management structure
 * @param format format string
 * @param channels number of channels
 * @param sample_size size in octets of a sample of an audio plane
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_sound_flow_alloc_def(struct uref_mgr *mgr,
                                                     const char *format,
                                                     uint8_t channels,
                                                     uint8_t sample_size)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL)) return NULL;
    if (unlikely(!(ubase_check(uref_flow_set_def_va(uref,
                            UREF_SOUND_FLOW_DEF "%s", format)) &&
                   ubase_check(uref_sound_flow_set_channels(uref, channels)) &&
                   ubase_check(uref_sound_flow_set_sample_size(uref,
                            sample_size)) &&
                   ubase_check(uref_sound_flow_set_planes(uref, 0))))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

/** @This registers a new plane in the sound flow definition packet.
 *
 * @param uref uref control packet
 * @param channel channel type (see channel reference)
 * @return an error code
 */
static inline int uref_sound_flow_add_plane(struct uref *uref,
                                            const char *channel)
{
    uint8_t plane = 0;
    if (unlikely(channel == NULL))
        return UBASE_ERR_INVALID;
    uref_sound_flow_get_planes(uref, &plane);
    UBASE_RETURN(uref_sound_flow_set_planes(uref, plane + 1))
    UBASE_RETURN(uref_sound_flow_set_channel(uref, channel, plane))
    return UBASE_ERR_NONE;
}

/** @internal @This finds a plane by its channel.
 *
 * @param uref uref control packet
 * @param channel channel type
 * @param plane_p written with the matching plane number
 * @return an error code
 */
static inline int uref_sound_flow_find_channel(struct uref *uref,
                                               const char *channel,
                                               uint8_t *plane_p)
{
    assert(channel != NULL);
    uint8_t planes = 0;
    uref_sound_flow_get_planes(uref, &planes);

    for (uint8_t plane = 0; plane < planes; plane++) {
        const char *plane_channel;
        UBASE_RETURN(uref_sound_flow_get_channel(uref, &plane_channel, plane))
        if (unlikely(!strcmp(channel, plane_channel))) {
            *plane_p = plane;
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_INVALID;
}

/** @This checks if there is a plane with the given properties.
 *
 * @param uref uref control packet
 * @param channel channel type
 * @return an error code
 */
static inline int uref_sound_flow_check_channel(struct uref *uref,
          const char *channel)
{
    uint8_t plane;
    return uref_sound_flow_find_channel(uref, channel, &plane);
}

/** @This copies the attributes defining the ubuf manager format to
 * another uref.
 *
 * @param uref_dst destination uref
 * @param uref_src source uref
 * @return an error code
 */
static inline int uref_sound_flow_copy_format(struct uref *uref_dst,
                                              struct uref *uref_src)
{
    const char *def;
    uint8_t planes, sample_size;
    UBASE_RETURN(uref_flow_get_def(uref_src, &def))
    UBASE_RETURN(uref_flow_set_def(uref_dst, def))
    UBASE_RETURN(uref_sound_flow_get_sample_size(uref_src, &sample_size))
    UBASE_RETURN(uref_sound_flow_set_sample_size(uref_dst, sample_size))
    UBASE_RETURN(uref_sound_flow_get_planes(uref_src, &planes))
    UBASE_RETURN(uref_sound_flow_set_planes(uref_dst, planes))

    for (uint8_t plane = 0; plane < planes; plane++) {
        const char *channel;
        UBASE_RETURN(uref_sound_flow_get_channel(uref_src, &channel, plane))
        UBASE_RETURN(uref_sound_flow_set_channel(uref_dst, channel, plane))
    }
    return UBASE_ERR_NONE;
}

/** @This clears the attributes defining the ubuf_sound manager format.
 *
 * @param uref uref control packet
 */
static inline void uref_sound_flow_clear_format(struct uref *uref)
{
    uint8_t planes;
    uref_sound_flow_delete_sample_size(uref);
    if (unlikely(!ubase_check(uref_sound_flow_get_planes(uref, &planes))))
        return;

    for (uint8_t plane = 0; plane < planes; plane++) {
        uref_sound_flow_delete_channel(uref, plane);
    }
    uref_sound_flow_delete_planes(uref);
}

/** @This compares the format flow definition between two urefs.
 *
 * @param uref1 first uref
 * @param uref2 second uref
 * @return true if both urefs describe the same format
 */
static inline bool uref_sound_flow_compare_format(struct uref *uref1,
                                                  struct uref *uref2)
{
    if (uref_flow_cmp_def(uref1, uref2) != 0 ||
        uref_sound_flow_cmp_sample_size(uref1, uref2) != 0 ||
        uref_sound_flow_cmp_planes(uref1, uref2) != 0)
        return false;

    uint8_t planes;
    UBASE_RETURN(uref_sound_flow_get_planes(uref1, &planes))
    for (uint8_t plane = 0; plane < planes; plane++) {
        if (uref_sound_flow_cmp_channel(uref1, uref2, plane) != 0)
            return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif
#endif

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

/** @internal flow definition prefix for packed sound allocator */
#define UREF_SOUND_FLOW_DEF "block."

UREF_ATTR_SMALL_UNSIGNED(sound_flow, channels, "s.channels", number of channels)
UREF_ATTR_SMALL_UNSIGNED(sound_flow, sample_size, "s.sample_size",
        size in octets of a sample of an audio channel)
UREF_ATTR_UNSIGNED(sound_flow, rate, "s.rate", samples per second)
UREF_ATTR_SMALL_UNSIGNED(sound_flow, prepend, "s.prepend",
        extra samples added before each channel)
UREF_ATTR_UNSIGNED(sound_flow, align, "s.align", alignment in octets)
UREF_ATTR_INT(sound_flow, align_offset, "s.align_offset",
        offset of the aligned sample)
UREF_ATTR_UNSIGNED(sound_flow, samples, "s.samples", number of samples)

/** @This allocates a control packet to define a new sound flow.
 *
 * @param mgr uref management structure
 * @param format format string
 * @param channels number of channels
 * @param sample_size size in octets of a sample of an audio channel
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
                            UREF_SOUND_FLOW_DEF "%s" "sound.", format)) &&
                   ubase_check(uref_sound_flow_set_channels(uref, channels)) &&
                   ubase_check(uref_sound_flow_set_sample_size(uref,
                            sample_size))))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

#include <stdint.h>
#include <stdbool.h>

/** @internal flow definition prefix for sound allocator */
#define UREF_SOUND_FLOW_DEF "sound."

UREF_ATTR_TEMPLATE(sound_flow, channels, "s.channels", small_unsigned, uint8_t, number of channels)
UREF_ATTR_TEMPLATE(sound_flow, sample_size, "s.sample_size", small_unsigned, uint8_t, size in octets of a sample of an audio channel)
UREF_ATTR_TEMPLATE(sound_flow, rate, "s.rate", unsigned, uint64_t, samples per second)
UREF_ATTR_TEMPLATE(sound_flow, prepend, "s.prepend", small_unsigned, uint8_t, extra samples added before each channel)
UREF_ATTR_TEMPLATE(sound_flow, align, "s.align", unsigned, uint64_t, alignment in octets)
UREF_ATTR_TEMPLATE(sound_flow, align_offset, "s.align_offset", int, int64_t, offset of the aligned sample)
UREF_ATTR_TEMPLATE(sound_flow, samples, "s.samples", unsigned, uint64_t, number of samples)

/** @This allocates a control packet to define a new sound flow.
 *
 * @param mgr uref management structure
 * @param channels number of channels
 * @param sample_size size in octets of a sample of an audio channel
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_sound_flow_alloc_def(struct uref_mgr *mgr,
                                                     uint8_t channels,
                                                     uint8_t sample_size)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL)) return NULL;
    if (unlikely(!(uref_flow_set_def(uref, UREF_SOUND_FLOW_DEF) &&
                   uref_sound_flow_set_channels(uref, channels) &&
                   uref_sound_flow_set_sample_size(uref, sample_size)))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#undef UREF_SOUND_FLOW_DEF

#endif

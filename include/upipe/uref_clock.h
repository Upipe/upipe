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
 * @short Upipe clock attributes for uref
 */

#ifndef _UPIPE_UREF_CLOCK_H_
/** @hidden */
#define _UPIPE_UREF_CLOCK_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>

UREF_ATTR_TEMPLATE(clock, systime, "k.systime", unsigned, uint64_t, reception time in system clock)
UREF_ATTR_TEMPLATE(clock, systime_rap, "k.systime.rap", unsigned, uint64_t, reception time in system clock of the last random access point)
UREF_ATTR_TEMPLATE(clock, pts, "k.pts", unsigned, uint64_t, presentation timestamp in Upipe clock)
UREF_ATTR_TEMPLATE(clock, pts_orig, "k.pts.orig", unsigned, uint64_t, original presentation timestamp in stream clock)
UREF_ATTR_TEMPLATE(clock, pts_sys, "k.pts.sys", unsigned, uint64_t, presentation timestamp in system clock)
UREF_ATTR_TEMPLATE(clock, dts, "k.dts", unsigned, uint64_t, decoding timestamp in Upipe clock)
UREF_ATTR_TEMPLATE(clock, dts_orig, "k.dts.orig", unsigned, uint64_t, original decoding timestamp in stream clock)
UREF_ATTR_TEMPLATE(clock, dts_sys, "k.dts.sys", unsigned, uint64_t, decoding timestamp in system clock)
UREF_ATTR_TEMPLATE(clock, vbv_delay, "k.vbvdelay", unsigned, uint64_t, vbv/dts delay)
UREF_ATTR_TEMPLATE(clock, duration, "k.duration", unsigned, uint64_t, duration)

#endif

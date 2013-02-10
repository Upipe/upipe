/*****************************************************************************
 * uref_clock.h: clock attributes for uref
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

#ifndef _UPIPE_UREF_CLOCK_H_
/** @hidden */
#define _UPIPE_UREF_CLOCK_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>

UREF_ATTR_TEMPLATE(clock, systime, "k.systime", unsigned, uint64_t, reception time)
UREF_ATTR_TEMPLATE(clock, pts, "k.pts", unsigned, uint64_t, presentation timestamp)
UREF_ATTR_TEMPLATE(clock, pts_orig, "k.pts.orig", unsigned, uint64_t, original presentation timestamp)
UREF_ATTR_TEMPLATE(clock, pts_sys, "k.pts.sys", unsigned, uint64_t, system presentation timestamp)
UREF_ATTR_TEMPLATE(clock, dts_delay, "k.dtsdelay", unsigned, uint64_t, dts/pts delay)
UREF_ATTR_TEMPLATE(clock, vbv_delay, "k.vbvdelay", unsigned, uint64_t, vbv/dts delay)
UREF_ATTR_TEMPLATE(clock, duration, "k.duration", unsigned, uint64_t, duration)

#endif

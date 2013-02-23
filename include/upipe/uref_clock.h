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

UREF_ATTR_UNSIGNED_SH(clock, systime, UDICT_TYPE_CLOCK_SYSTIME,
        reception time in system clock)
UREF_ATTR_UNSIGNED_SH(clock, systime_rap, UDICT_TYPE_CLOCK_SYSTIME_RAP,
        reception time in system clock of the last random access point)
UREF_ATTR_UNSIGNED_SH(clock, pts, UDICT_TYPE_CLOCK_PTS,
        presentation timestamp in Upipe clock)
UREF_ATTR_UNSIGNED_SH(clock, pts_orig, UDICT_TYPE_CLOCK_PTS_ORIG,
        original presentation timestamp in stream clock)
UREF_ATTR_UNSIGNED_SH(clock, pts_sys, UDICT_TYPE_CLOCK_PTS_SYS,
        presentation timestamp in system clock)
UREF_ATTR_UNSIGNED_SH(clock, dts, UDICT_TYPE_CLOCK_DTS,
        decoding timestamp in Upipe clock)
UREF_ATTR_UNSIGNED_SH(clock, dts_orig, UDICT_TYPE_CLOCK_DTS_ORIG,
        original decoding timestamp in stream clock)
UREF_ATTR_UNSIGNED_SH(clock, dts_sys, UDICT_TYPE_CLOCK_DTS_SYS,
        decoding timestamp in system clock)
UREF_ATTR_UNSIGNED_SH(clock, vbv_delay, UDICT_TYPE_CLOCK_VBVDELAY,
        vbv/dts delay)
UREF_ATTR_UNSIGNED_SH(clock, duration, UDICT_TYPE_CLOCK_DURATION, duration)

#endif

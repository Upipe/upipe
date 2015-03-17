/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe ebur128
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_EBUR128_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_EBUR128_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe/uref_attr.h>
#include <stdint.h>

UREF_ATTR_FLOAT(ebur128, momentary, "ebur128.momentary", momentary loudness)
UREF_ATTR_FLOAT(ebur128, lra, "ebur128.lra", loudness range)
UREF_ATTR_FLOAT(ebur128, global, "ebur128.global", global integrated loudness)

#define UPIPE_FILTER_EBUR128_SIGNATURE UBASE_FOURCC('r', '1', '2', '8')

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_ebur128_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

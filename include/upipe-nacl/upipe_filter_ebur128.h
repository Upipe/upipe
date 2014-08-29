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
#include <ppapi_simple/ps.h>
#define UPIPE_FILTER_EBUR128_SIGNATURE UBASE_FOURCC('r', '1', '2', '8')

enum upipe_filter_ebur128_command {

    UPIPE_FILTER_EBUR127_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_FILTER_EBUR127_SET_TIME_LENGTH,
};


/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_ebur128_mgr_alloc(void);

static inline struct upipe *_upipe_filter_ebur128_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe, int n_pipe)
{
    return upipe_alloc(mgr, uprobe, UPIPE_FILTER_EBUR128_SIGNATURE,n_pipe);
}

static inline enum ubase_err upipe_filter_ebur128_set_time_length(struct upipe *upipe, int time_ms)
{
    return upipe_control(upipe, UPIPE_FILTER_EBUR127_SET_TIME_LENGTH, UPIPE_FILTER_EBUR128_SIGNATURE, time_ms);
}

#ifdef __cplusplus
}
#endif
#endif

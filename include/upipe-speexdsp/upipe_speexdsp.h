/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré <funman@videolan.org>
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
 * @short Upipe speexdsp resampler module
 */

#ifndef _UPIPE_SPEEXDSP_UPIPE_SPEEXDSP_H_
/** @hidden */
#define _UPIPE_SPEEXDSP_UPIPE_SPEEXDSP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SPEEXDSP_SIGNATURE UBASE_FOURCC('s','p','x','d')

enum upipe_speexdsp_command {
    UPIPE_SPEEXDSP_SENTINAL = UPIPE_CONTROL_LOCAL,

    UPIPE_SPEEXDSP_RESET_RESAMPLER, /* int sig */
};

static inline int upipe_speexdsp_reset_resampler(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_SPEEXDSP_RESET_RESAMPLER, UPIPE_SPEEXDSP_SIGNATURE);
}

/** @This returns the management structure for speexdsp pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_speexdsp_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

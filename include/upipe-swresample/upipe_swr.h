/*****************************************************************************
 * upipe_swr.h: application interface for swr module
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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

/** @file
 * @short Upipe swrcale (ffmpeg) module
 */

#ifndef _UPIPE_MODULES_UPIPE_SWR_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SWR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SWR_SIGNATURE UBASE_FOURCC('s','w','r',' ')

/** @This extends upipe_command with specific commands for avcodec decode. */
enum upipe_swr_command {
    UPIPE_SWR_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set format (const char*) */
    UPIPE_SWR_SET_FMT,
};

/** @This sets the output format.
 *
 * @param upipe description structure of the pipe
 * @param fmt audio format
 * @return false in case of error
 */
static inline bool upipe_swr_set_fmt(struct upipe *upipe, const char *fmt)
{
    return upipe_control(upipe, UPIPE_SWR_SET_FMT, UPIPE_SWR_SIGNATURE, fmt);
}

/** @This returns the management structure for swr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_swr_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

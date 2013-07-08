/*****************************************************************************
 * upipe_sws.h: application interface for sws module
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
 * @short Upipe swscale (ffmpeg) module
 */

#ifndef _UPIPE_MODULES_UPIPE_SWS_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SWS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SWS_SIGNATURE UBASE_FOURCC('s','w','s',' ')

/** @This extends upipe_command with specific commands for avcodec decode. */
enum upipe_sws_command {
    UPIPE_SWS_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set size (int, int) */
    UPIPE_SWS_SET_SIZE,
    /** get size (int*, int*) */
    UPIPE_SWS_GET_SIZE,
};

/** @This sets the output dimensions.
 *
 * @param upipe description structure of the pipe
 * @param size size parameter (0=disabled)
 * @return false in case of error
 */
static inline bool upipe_sws_set_size(struct upipe *upipe,
                                      int hsize, int vsize)
{
    return upipe_control(upipe, UPIPE_SWS_SET_SIZE, UPIPE_SWS_SIGNATURE,
                         hsize, vsize);
}

/** @This gets the output dimensions.
 * If some codec is already used, it is re-opened.
 *
 * @param upipe description structure of the pipe
 * @param size size parameter (0=disabled)
 * @return false in case of error
 */
static inline bool upipe_sws_get_size(struct upipe *upipe,
                                      int *hsize_p, int *vsize_p)
{
    return upipe_control(upipe, UPIPE_SWS_GET_SIZE, UPIPE_SWS_SIGNATURE,
                         hsize_p, vsize_p);
}

/** @This returns the management structure for sws pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

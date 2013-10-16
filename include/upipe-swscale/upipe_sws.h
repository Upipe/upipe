/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 */

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

    /** set flags (int) */
    UPIPE_SWS_SET_FLAGS,
    /** get flags (int *) */
    UPIPE_SWS_GET_FLAGS
};

/** @This gets the swscale flags.
 *
 * @param upipe description structure of the pipe
 * @param flags_p filled in with the swscale flags
 * @return false in case of error
 */
static inline bool upipe_sws_get_flags(struct upipe *upipe, int *flags_p)
{
    return upipe_control(upipe, UPIPE_SWS_GET_FLAGS, UPIPE_SWS_SIGNATURE,
                         flags_p);
}

/** @This sets the swscale flags.
 *
 * @param upipe description structure of the pipe
 * @param flags swscale flags
 * @return false in case of error
 */
static inline bool upipe_sws_set_flags(struct upipe *upipe, int flags)
{
    return upipe_control(upipe, UPIPE_SWS_SET_FLAGS, UPIPE_SWS_SIGNATURE,
                         flags);
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

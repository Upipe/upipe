/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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

#ifndef _UPIPE_MODULES_UPIPE_VBLK_H_
#define _UPIPE_MODULES_UPIPE_VBLK_H_

#include <upipe/ubase.h>

/** @This is the signature of a video blank pipe. */
#define UPIPE_VBLK_SIGNATURE    UBASE_FOURCC('v','b','l','k')

/** @This enumerates the video blank local control commands. */
enum upipe_vblk_command {
    /** sentinel */
    UPIPE_VBLK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the reference picture (struct uref *) */
    UPIPE_VBLK_SET_PIC,
};

/** @This sets the reference picture to output.
 *
 * @param upipe description structure of the pipe
 * @param uref picture buffer
 * @return an error code
 */
static inline int upipe_vblk_set_pic(struct upipe *upipe, struct uref *uref)
{
    return upipe_control(upipe, UPIPE_VBLK_SET_PIC, UPIPE_VBLK_SIGNATURE, uref);
}

/** @This returns the video blank pipe manager.
 *
 * @return a pointer to the video blank pipe manager
 */
struct upipe_mgr *upipe_vblk_mgr_alloc(void);

#endif /* !_UPIPE_MODULES_UPIPE_VBLK_H_ */

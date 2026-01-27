/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_VIDEO_BLANK_H_
#define _UPIPE_MODULES_UPIPE_VIDEO_BLANK_H_

#include "upipe/upipe.h"

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

#endif /* !_UPIPE_MODULES_UPIPE_VIDEO_BLANK_H_ */

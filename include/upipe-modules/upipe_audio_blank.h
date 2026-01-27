/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_AUDIO_BLANK_H_
# define _UPIPE_MODULES_UPIPE_AUDIO_BLANK_H_

#include "upipe/upipe.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_ABLK_SIGNATURE    UBASE_FOURCC('a','b','l','k')

/** @This enumerates the audio blank control commands. */
enum upipe_ablk_command {
    /** sentinel */
    UPIPE_ABLK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the reference sound (struct uref *) */
    UPIPE_ABLK_SET_SOUND,
};

/** @This sets the reference sound to output.
 *
 * @param upipe description structure of the pipe
 * @param uref sound buffer
 * @return an error code
 */
static inline int upipe_ablk_set_sound(struct upipe *upipe, struct uref *uref)
{
    return upipe_control(upipe, UPIPE_ABLK_SET_SOUND, UPIPE_ABLK_SIGNATURE,
                         uref);
}

/** @This returns the audio blank pipe manager.
 *
 * @return a pipe manager
 */
struct upipe_mgr *upipe_ablk_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

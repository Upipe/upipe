/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe GL - common definitions
 */

#ifndef _UPIPE_GL_UPIPE_GL_SINK_COMMON_H_
/** @hidden */
#define _UPIPE_GL_UPIPE_GL_SINK_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_GL_SINK_SIGNATURE UBASE_FOURCC('g', 'l', 's', 'k')

/** @This extends uprobe_event with specific events for gl sink. */
enum uprobe_gl_sink_event {
    UPROBE_GL_SINK_SENTINEL = UPROBE_LOCAL,

    /** init GL context (int width, int height) */
    UPROBE_GL_SINK_INIT,
    /** render GL (struct uref *) */
    UPROBE_GL_SINK_RENDER,
    /** reshape GL (int width, int height) */
    UPROBE_GL_SINK_RESHAPE,

    UPROBE_GL_SINK_LOCAL
};

/** @This throws an UPROBE_GL_SINK_RENDER event.
 *
 * @param upipe pointer to pipe throwing the event
 * @param uref uref structure describing the picture
 * @return an error code
 */
static inline int upipe_gl_sink_throw_render(struct upipe *upipe,
                                             struct uref *uref)
{
    return upipe_throw(upipe, UPROBE_GL_SINK_RENDER,
                       UPIPE_GL_SINK_SIGNATURE, uref);
}

/** @This extends upipe_command with specific commands for gl sink. */
enum upipe_gl_sink_command {
    UPIPE_GL_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_GL_SINK_CONTROL_LOCAL
};

/** @This loads a uref picture into the specified texture
 * @param uref uref structure describing the picture
 * @param texture GL texture
 * @return false in case of error
 */
bool upipe_gl_texture_load_uref(struct uref *uref, unsigned int texture);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
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
 * @short Upipe GL - common definitions
 */

#ifndef _UPIPE_GL_UPIPE_GL_SINK_COMMON_H_
/** @hidden */
#define _UPIPE_GL_UPIPE_GL_SINK_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

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

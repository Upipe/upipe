/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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

#include <upipe/upipe.h>

#define UPIPE_GL_SINK_SIGNATURE UBASE_FOURCC('g', 'l', 's', 'k')

/** @This extends uprobe_event with specific events for gl sink. */
enum uprobe_gl_sink_event {
    UPROBE_GL_SINK_SENTINEL = UPROBE_LOCAL,

    /** init GL context */
    UPROBE_GL_SINK_INIT,
    /** render GL */
    UPROBE_GL_SINK_RENDER,
    /** reshape GL  */
    UPROBE_GL_SINK_RESHAPE,

    UPROBE_GL_SINK_LOCAL
};

/** @This extends upipe_command with specific commands for gl sink. */
enum upipe_gl_sink_command {
    UPIPE_GL_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** return the texture identifier (unsigned int *) */
    UPIPE_GL_SINK_GET_TEXTURE,
    /** sets the texture identifier (unsigned int) */
    UPIPE_GL_SINK_SET_TEXTURE,

    UPIPE_GL_SINK_CONTROL_LOCAL
};

/** @This returns the texture identifier from the pipe. It should only be called from
 * gl callbacks (init, render, reshape).
 *
 * @param upipe description structure of the pipe
 * @param texture_p pointer to texture identifier
 * @return false in case of error
 */
static inline bool upipe_gl_sink_get_texture(struct upipe *upipe, unsigned int *texture_p)
{
    return upipe_control(upipe, UPIPE_GL_SINK_GET_TEXTURE, UPIPE_GL_SINK_SIGNATURE,
                         texture_p);
}

/** @This sets the texture identifier. It should only be called from the
 * gl init callback
 *
 * @param upipe description structure of the pipe
 * @param texture texture identifier
 * @return false in case of error
 */
static inline bool upipe_gl_sink_set_texture(struct upipe *upipe, unsigned int texture)
{
    return upipe_control(upipe, UPIPE_GL_SINK_SET_TEXTURE, UPIPE_GL_SINK_SIGNATURE,
                         texture);
}

#endif

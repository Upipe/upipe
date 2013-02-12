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
 * @short Upipe GLX (OpenGL/X11) sink module.
 * This sink module renders rgb pics in a GL/X window. This is typically
 * used by a player.
 * It must be given a specific probe at allocation to catch GL events
 * (init, render, reshape) defined in <upipe-gl/upipe_gl_sink_common.h>.
 * Application developers can either use a predefined adhoc probe or use
 * their own probe structure.
 * uprobe_gl_sink_cube is currently provided as an example.
 * Please read tests/upipe_glx_sink_test.c. You can also read
 * examples/glxplay.c for a more complete example.
 */

#ifndef _UPIPE_GL_UPIPE_GLX_SINK_H_
/** @hidden */
#define _UPIPE_GL_UPIPE_GLX_SINK_H_

#include <upipe/upipe.h>
#include <upipe-gl/upipe_gl_sink_common.h>

#define UPIPE_GLX_SINK_SIGNATURE UBASE_FOURCC('g', 'l', 'x', 's')

/** @This extends uprobe_event with specific events for glx sink. */
enum uprobe_glx_sink_event {
    UPROBE_GLX_SINK_SENTINEL = UPROBE_GL_SINK_LOCAL,

    /** received keypress event (long keysym) */
    UPROBE_GLX_SINK_KEYPRESS,
    /** received keyrelease event (long keysym) */
    UPROBE_GLX_SINK_KEYRELEASE,
};

/** @This extends upipe_command with specific commands for glx sink. */
enum upipe_glx_sink_command {
    UPIPE_GLX_SINK_SENTINEL = UPIPE_GL_SINK_CONTROL_LOCAL,

    /** launch glx with window size and position (int, int, int, int) */
    UPIPE_GLX_SINK_INIT,
    /** returns the current window size (int *, int *) */
    UPIPE_GLX_SINK_GET_SIZE,
    /** set window size (int, int) */
    UPIPE_GLX_SINK_SET_SIZE,
};

/** @This inits the glx window/context and displays it
 *
 * @param upipe description structure of the pipe
 * @param x window position x
 * @param y window position y
 * @param width window width
 * @param height window height
 * @return false in case of error
 */
static inline bool upipe_glx_sink_init(struct upipe *upipe, int x, int y,
                                       int width, int height)
{
    return upipe_control(upipe, UPIPE_GLX_SINK_INIT, UPIPE_GLX_SINK_SIGNATURE,
                         x, y, width, height);
}

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_glx_sink_mgr_alloc(void);

#endif

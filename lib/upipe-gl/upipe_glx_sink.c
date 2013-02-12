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
 * @short Upipe GLX (OpenGL/X11) sink module
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe-gl/upipe_glx_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>

struct upipe_glx_sink {
    /** X display */
    Display *display;
    /** X visual information */
    XVisualInfo *visual;
    /** X window */
    Window window;
    /** GLX context */
    GLXContext glxContext;
    /** GL texture */
    GLuint texture;
    /** doublebuffer available */
    bool doublebuffered;
    /** X event mask */
    long eventmask;

    /** frame counter */
    uint64_t counter;
    /** theta */
    float theta;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** event loop watcher */
    struct upump *upump_watcher;
    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_glx_sink, upipe);
UPIPE_HELPER_UPUMP_MGR(upipe_glx_sink, upump_mgr, upump_watcher)

static inline void upipe_glx_sink_flush(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    if (upipe_glx_sink->doublebuffered) {
        glXSwapBuffers(upipe_glx_sink->display, upipe_glx_sink->window);
    } else {
        glFlush();
    }
}

/** @internal @This handles keystrokes.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static inline void upipe_glx_sink_keyhandler(struct upipe *upipe,  XEvent *evt,
                                      enum uprobe_glx_sink_event probe_event)
{
    int n, i;
    char buf[8];
    KeySym sym;

    n = XLookupString((XKeyEvent *)evt, buf, sizeof(buf), &sym, NULL);
    if (n > 0) {
        for (i = 0; i < n; i++) {
            upipe_throw(upipe, probe_event,
                    UPIPE_GLX_SINK_SIGNATURE, (unsigned long) buf[i]);
        }
    } else {
        upipe_throw(upipe, probe_event,
                UPIPE_GLX_SINK_SIGNATURE, (unsigned long) sym);
    }
}

/** @internal @This does the actual event processing
 * @param upipe description structure of the pipe
 */
static void upipe_glx_sink_event_process(struct upipe *upipe)
{
    XWindowAttributes winAttr;
    XEvent evt;
    struct upipe_glx_sink *glx = upipe_glx_sink_from_upipe(upipe);

    while(XCheckWindowEvent(glx->display, glx->window,
                            glx->eventmask, &evt)) {
        switch (evt.type) {
            case Expose: {
                XGetWindowAttributes(glx->display, glx->window, &winAttr);

                glXMakeCurrent(glx->display, glx->window,
                               glx->glxContext);
                upipe_throw(upipe, UPROBE_GL_SINK_RESHAPE,
                            UPIPE_GL_SINK_SIGNATURE, winAttr.width, winAttr.height);
                upipe_glx_sink_flush(upipe);
                break;
            }
            case KeyRelease:
                upipe_glx_sink_keyhandler(upipe, &evt, UPROBE_GLX_SINK_KEYRELEASE);
                break;
            case KeyPress: 
                upipe_glx_sink_keyhandler(upipe, &evt, UPROBE_GLX_SINK_KEYPRESS);
                break;
            default:
                break;
        }
    }
}

/** @internal @This is our upump watcher callback.
 * @param upump description structure of the pump
 */
static void upipe_glx_sink_watcher_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_glx_sink *glx = upipe_glx_sink_from_upipe(upipe);

    if (!glx->display) {
        upipe_warn(upipe, "glx context not ready");
        return;
    }
    upipe_glx_sink_event_process(upipe);
}

/** @internal @This sets the watcher
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static bool upipe_glx_sink_init_watcher(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    if (upipe_glx_sink->upump_mgr && upipe_glx_sink->display) {
        struct upump *upump = upump_alloc_fd_read(upipe_glx_sink->upump_mgr,
                                upipe_glx_sink_watcher_cb, upipe, false,
                                XConnectionNumber(upipe_glx_sink->display));
        if (unlikely(!upump)) {
            return false;
        } else {
            upipe_glx_sink_set_upump(upipe, upump);
            upump_start(upump);
        }
    }
    return true;
}

/** @internal @This inits the X window and glx context
 * @param upipe description structure of the pipe
 * @param x window horizontal position
 * @param y window vertical position
 * @param width window width
 * @param height window height
 * @return false in case of error
 */
static bool upipe_glx_sink_init_glx(struct upipe *upipe, int x, int y, int width, int height)
{
    int doubleBufferV[] =
    {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        None
    };
    int singleBufferV[] =
    {
        GLX_RGBA,
        None
    };

    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    Display *display = NULL;
    XVisualInfo *visual = NULL;
    Window window = 0;
    GLXContext glxContext;
    XSetWindowAttributes winAttr;
    Colormap colorMap;
    int errorBase, eventBase;

    display = XOpenDisplay(NULL);
    if (unlikely(!display)) {
        upipe_err(upipe, "Could not open X display");
        return false;
    }
    if (!glXQueryExtension(display, &errorBase, &eventBase)) {
        upipe_err(upipe, "X server has no GLX extension");
        return false;
    }

    upipe_glx_sink->doublebuffered = true;
    visual = glXChooseVisual(display, DefaultScreen(display), doubleBufferV);
    if (unlikely(!visual)) {
        upipe_warn(upipe, "No double buffer support, trying single");
        visual = glXChooseVisual(display, DefaultScreen(display), singleBufferV);
        if (unlikely(!visual)) {
            upipe_err(upipe, "No single buffer either, aborting");
            XCloseDisplay(display);
            return false;
        }
        upipe_glx_sink->doublebuffered = false;
    }

    glxContext = glXCreateContext(display, visual, NULL, GL_TRUE);
    if (unlikely(!glxContext)) {
        upipe_err(upipe, "Could not create GLX context.");
        XCloseDisplay(display);
        return false;
    }

    upipe_glx_sink->eventmask = KeyPressMask | KeyReleaseMask | ExposureMask;

    // Create colormap and init window attributes
    colorMap = XCreateColormap(display, RootWindow(display, visual->screen),
                                        visual->visual, AllocNone);
    winAttr.colormap = colorMap;
    winAttr.border_pixel = 0;
    winAttr.event_mask = upipe_glx_sink->eventmask;

    window = XCreateWindow(display, RootWindow(display, visual->screen),
                           x, y, width, height, 0,
                           visual->depth, InputOutput, visual->visual,
                           CWBorderPixel | CWColormap | CWEventMask,
                           &winAttr);
    XStoreName(display, window, "upipe-glx");
    glXMakeCurrent(display, window, glxContext);
    XMapRaised(display, window);
    
    upipe_glx_sink->visual = visual;
    upipe_glx_sink->display = display;
    upipe_glx_sink->window = window;
    upipe_glx_sink->glxContext = glxContext;

    // Now init GL context
    upipe_throw(upipe, UPROBE_GL_SINK_INIT,
                UPIPE_GL_SINK_SIGNATURE, width, height);
    upipe_glx_sink_flush(upipe);

    // Set watcher (in case _set_upump_mgr was called before _init_glx())
    upipe_glx_sink_init_watcher(upipe);

    // Go through event process once to actually create the X window
    upipe_glx_sink_event_process(upipe);

    return true;
}

/** @internal @This cleans everything GLX-related
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static void upipe_glx_sink_clean_glx(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);

    glDeleteTextures(1, &upipe_glx_sink->texture);
    glXMakeCurrent(upipe_glx_sink->display, None, NULL);
    glXDestroyContext(upipe_glx_sink->display, upipe_glx_sink->glxContext);
    XDestroyWindow(upipe_glx_sink->display, upipe_glx_sink->window);
//    XFree(upipe_glx_sink->visual);
    XCloseDisplay(upipe_glx_sink->display);
}

/** @internal @This allocates a glx_sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_glx_sink_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_glx_sink *upipe_glx_sink = malloc(sizeof(struct upipe_glx_sink));
    if (unlikely(upipe_glx_sink == NULL)) {
        return NULL;
    }
    struct upipe *upipe = upipe_glx_sink_to_upipe(upipe_glx_sink);
    upipe_init(upipe, mgr, uprobe);
    urefcount_init(&upipe_glx_sink->refcount);
    upipe_glx_sink_init_upump_mgr(upipe);

    upipe_glx_sink->display = NULL;
    upipe_glx_sink->visual = NULL;
    upipe_glx_sink->counter = 0;
    upipe_glx_sink->theta = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This handles input pics.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_glx_sink_input_pic(struct upipe *upipe, struct uref *uref,
                                     struct upump *upump)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);

    const uint8_t *data = NULL;
    size_t width, height;
    uref_pic_size(uref, &width, &height, NULL);
    if(!uref_pic_plane_read(uref, "rgb24", 0, 0, -1, -1, &data)) {
        upipe_err(upipe, "Could not map picture plane");
        return;
    }
    glBindTexture(GL_TEXTURE_2D, upipe_glx_sink->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, 
            height, 0, GL_RGB, GL_UNSIGNED_BYTE, 
            data);
    uref_pic_plane_unmap(uref, "rgb24", 0, 0, -1, -1);

    glXMakeCurrent(upipe_glx_sink->display, upipe_glx_sink->window,
                   upipe_glx_sink->glxContext);
    upipe_throw(upipe, UPROBE_GL_SINK_RENDER,
                UPIPE_GL_SINK_SIGNATURE, width, height);
    upipe_glx_sink_flush(upipe);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump upump structure
 */
static void upipe_glx_sink_input(struct upipe *upipe, struct uref *uref,
                                 struct upump *upump)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);

    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_dbg_va(upipe, "ignoring flow def (%s)", def);
        uref_free(uref);
        return;
    }
    if (unlikely(!uref->ubuf)) { // no ubuf in uref
        uref_free(uref);
        return;
    }

    if (upipe_glx_sink->display) {
        upipe_glx_sink_input_pic(upipe, uref, upump);
    } else {
        upipe_warn(upipe, "glx context not ready");
    }
    uref_free(uref);
}

/** @internal @This initialize the upump watcher
 *
 * @param upipe description structure of the pipe
 * @param upump_mgr upump manager structure
 * @return false in case of errur
 */
static bool _upipe_glx_sink_set_upump_mgr(struct upipe *upipe,
                                          struct upump_mgr *upump_mgr)
{
    if (unlikely(!upipe_glx_sink_set_upump_mgr(upipe, upump_mgr))) {
        upipe_err(upipe, "Could not set upump manager");
        return false;
    }
    return upipe_glx_sink_init_watcher(upipe);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_glx_sink_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    struct upipe_glx_sink *glx = upipe_glx_sink_from_upipe(upipe);
    switch (command) {
        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_glx_sink_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return _upipe_glx_sink_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GLX_SINK_INIT: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GLX_SINK_SIGNATURE);
            int x = va_arg(args, int);
            int y = va_arg(args, int);
            int width = va_arg(args, int);
            int height = va_arg(args, int);
            return upipe_glx_sink_init_glx(upipe, x, y, width, height);
        }
        case UPIPE_GL_SINK_GET_TEXTURE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            unsigned int *texture_p = va_arg(args, unsigned int*);
            if (!glx->display) {
                return false;
            }
            *texture_p = (unsigned int) glx->texture;
            return true;
        }
        case UPIPE_GL_SINK_SET_TEXTURE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            unsigned int texture = va_arg(args, unsigned int);
            if (!glx->display) {
                return false;
            }
            glx->texture = (GLuint) texture;
            return true;
        }

        default:
            return false;
    }
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_glx_sink_use(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    urefcount_use(&upipe_glx_sink->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_glx_sink_release(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_glx_sink->refcount))) {
        upipe_throw_dead(upipe);
        upipe_glx_sink_clean_upump_mgr(upipe);
        if (upipe_glx_sink->display) {
            upipe_glx_sink_clean_glx(upipe);
        }
        upipe_clean(upipe);
        urefcount_clean(&upipe_glx_sink->refcount);
        free(upipe_glx_sink);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_glx_sink_mgr = {
    .signature = UPIPE_GLX_SINK_SIGNATURE,

    .upipe_alloc = upipe_glx_sink_alloc,
    .upipe_input = upipe_glx_sink_input,
    .upipe_control = upipe_glx_sink_control,
    .upipe_use = upipe_glx_sink_use,
    .upipe_release = upipe_glx_sink_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_glx_sink_mgr_alloc(void)
{
    return &upipe_glx_sink_mgr;
}

/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/ubuf.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
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

/** @hidden */
static bool upipe_glx_sink_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);
/** @hidden */
static void upipe_glx_sink_write_watcher(struct upump *upump);

/** @internal upipe_glx_sink private structure */
struct upipe_glx_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** delay applied to pts attribute when uclock is provided */
    uint64_t latency;
    /** X display */
    Display *display;
    /** X visual information */
    XVisualInfo *visual;
    /** X window */
    Window window;
    /** GLX context */
    GLXContext glxContext;
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
    /** event watcher */
    struct upump *upump_watcher;
    /** write watcher */
    struct upump *upump;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_glx_sink, upipe, UPIPE_GLX_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_glx_sink, urefcount, upipe_glx_sink_free)
UPIPE_HELPER_VOID(upipe_glx_sink)
UPIPE_HELPER_UPUMP_MGR(upipe_glx_sink, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_glx_sink, upump, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_glx_sink, upump_watcher, upump_mgr)
UPIPE_HELPER_INPUT(upipe_glx_sink, urefs, nb_urefs, max_urefs, blockers, upipe_glx_sink_output)
UPIPE_HELPER_UCLOCK(upipe_glx_sink, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

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
    upipe_use(upipe);

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
    upipe_release(upipe);
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
        struct upump *upump = upump_alloc_timer(upipe_glx_sink->upump_mgr,
                upipe_glx_sink_watcher_cb, upipe, upipe->refcount,
                27000000/1000, 27000000/1000);
        if (unlikely(!upump)) {
            return false;
        } else {
            upipe_glx_sink_set_upump_watcher(upipe, upump);
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
 * @return an error code
 */
static int upipe_glx_sink_init_glx(struct upipe *upipe, int x, int y, int width, int height)
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
        return UBASE_ERR_EXTERNAL;
    }
    if (!glXQueryExtension(display, &errorBase, &eventBase)) {
        upipe_err(upipe, "X server has no GLX extension");
        return UBASE_ERR_EXTERNAL;
    }

    upipe_glx_sink->doublebuffered = true;
    visual = glXChooseVisual(display, DefaultScreen(display), doubleBufferV);
    if (unlikely(!visual)) {
        upipe_warn(upipe, "No double buffer support, trying single");
        visual = glXChooseVisual(display, DefaultScreen(display), singleBufferV);
        if (unlikely(!visual)) {
            upipe_err(upipe, "No single buffer either, aborting");
            XCloseDisplay(display);
            return UBASE_ERR_EXTERNAL;
        }
        upipe_glx_sink->doublebuffered = false;
    }

    glxContext = glXCreateContext(display, visual, NULL, GL_TRUE);
    if (unlikely(!glxContext)) {
        upipe_err(upipe, "Could not create GLX context.");
        XCloseDisplay(display);
        return UBASE_ERR_EXTERNAL;
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

    upipe_glx_sink_check_upump_mgr(upipe);

    // Set watcher (in case _set_upump_mgr was called before _init_glx())
    upipe_glx_sink_init_watcher(upipe);

    // Go through event process once to actually create the X window
    upipe_glx_sink_event_process(upipe);

    return UBASE_ERR_NONE;
}

/** @internal @This cleans everything GLX-related
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static void upipe_glx_sink_clean_glx(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);

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
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_glx_sink_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_glx_sink_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    upipe_glx_sink_init_urefcount(upipe);
    upipe_glx_sink_init_upump_mgr(upipe);
    upipe_glx_sink_init_upump(upipe);
    upipe_glx_sink_init_upump_watcher(upipe);
    upipe_glx_sink_init_input(upipe);
    upipe_glx_sink_init_uclock(upipe);
    upipe_glx_sink->latency = 0;

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
 * @param upump_p reference to upump structure
 */
static bool upipe_glx_sink_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_glx_sink->latency = 0;
        uref_clock_get_latency(uref, &upipe_glx_sink->latency);
        /* FIXME throw new flow definition to update probe */
        upipe_throw_new_flow_def(upipe, uref);

        uref_free(uref);
        return true;
    }

    if (likely(upipe_glx_sink->uclock != NULL)) {
        uint64_t pts = 0;
        if (likely(ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
            pts += upipe_glx_sink->latency;
            uint64_t now = uclock_now(upipe_glx_sink->uclock);
            if (now < pts) {
                upipe_verbose_va(upipe, "sleeping %"PRIu64" (%"PRIu64")",
                                 pts - now, pts);
                upipe_glx_sink_wait_upump(upipe, pts - now,
                                          upipe_glx_sink_write_watcher);
                return false;
            } else if (now > pts + UCLOCK_FREQ / 10) {
                upipe_warn_va(upipe, "late picture dropped (%"PRId64")",
                              (now - pts) * 1000 / UCLOCK_FREQ);
                uref_free(uref);
                return true;
            }
        } else
            upipe_warn(upipe, "received non-dated buffer");
    }

    glXMakeCurrent(upipe_glx_sink->display, upipe_glx_sink->window,
                   upipe_glx_sink->glxContext);
    upipe_throw(upipe, UPROBE_GL_SINK_RENDER,
                UPIPE_GL_SINK_SIGNATURE, uref);
    upipe_glx_sink_flush(upipe);
    uref_free(uref);
    return true;
}

/** @internal @This is called when the picture should be displayed.
 *
 * @param upump description structure of the watcher
 */
static void upipe_glx_sink_write_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_glx_sink_set_upump(upipe, NULL);
    upipe_glx_sink_output_input(upipe);
    upipe_glx_sink_unblock_input(upipe);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_glx_sink_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);

    if (!upipe_glx_sink->display) {
        upipe_warn(upipe, "glx context not ready");
        uref_free(uref);
        return;
    }

    if (!upipe_glx_sink_check_input(upipe) ||
        !upipe_glx_sink_output(upipe, uref, upump_p)) {
        upipe_glx_sink_hold_input(upipe, uref);
        upipe_glx_sink_block_input(upipe, upump_p);
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_glx_sink_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    /* for the moment we only support rgb24 */
    uint8_t macropixel;
    if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
        macropixel != 1 ||
        !ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 3, "r8g8b8"))) {
        upipe_err(upipe, "incompatible flow definition");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_INVALID;
    }

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_glx_sink_provide_flow_format(struct upipe *upipe,
                                              struct urequest *request)
{
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_pic_flow_clear_format(flow_format);
    uref_pic_flow_set_macropixel(flow_format, 1);
    uref_pic_flow_set_planes(flow_format, 0);
    uref_pic_flow_add_plane(flow_format, 1, 1, 3, "r8g8b8");
    uref_pic_set_progressive(flow_format);
    uref_pic_flow_delete_colour_primaries(flow_format);
    uref_pic_flow_delete_transfer_characteristics(flow_format);
    uref_pic_flow_delete_matrix_coefficients(flow_format);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_glx_sink_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_glx_sink_set_upump(upipe, NULL);
            upipe_glx_sink_set_upump_watcher(upipe, NULL);
            UBASE_RETURN(upipe_glx_sink_attach_upump_mgr(upipe))
            upipe_glx_sink_init_watcher(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_ATTACH_UCLOCK:
            upipe_glx_sink_set_upump(upipe, NULL);
            upipe_glx_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_FLOW_FORMAT)
                return upipe_glx_sink_provide_flow_format(upipe, request);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_glx_sink_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_glx_sink_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_glx_sink_set_max_length(upipe, max_length);
        }

        case UPIPE_GLX_SINK_INIT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GLX_SINK_SIGNATURE)
            int x = va_arg(args, int);
            int y = va_arg(args, int);
            int width = va_arg(args, int);
            int height = va_arg(args, int);
            return upipe_glx_sink_init_glx(upipe, x, y, width, height);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_glx_sink_free(struct upipe *upipe)
{
    struct upipe_glx_sink *upipe_glx_sink = upipe_glx_sink_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_glx_sink_clean_upump(upipe);
    upipe_glx_sink_clean_upump_watcher(upipe);
    upipe_glx_sink_clean_upump_mgr(upipe);
    if (upipe_glx_sink->display) {
        upipe_glx_sink_clean_glx(upipe);
    }
    upipe_glx_sink_clean_uclock(upipe);
    upipe_glx_sink_clean_input(upipe);
    upipe_glx_sink_clean_urefcount(upipe);
    upipe_glx_sink_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_glx_sink_mgr = {
    .refcount = NULL,
    .signature = UPIPE_GLX_SINK_SIGNATURE,

    .upipe_alloc = upipe_glx_sink_alloc,
    .upipe_input = upipe_glx_sink_input,
    .upipe_control = upipe_glx_sink_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for glx_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_glx_sink_mgr_alloc(void)
{
    return &upipe_glx_sink_mgr;
}

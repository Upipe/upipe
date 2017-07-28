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

/** @file
 * @short Upipe GL sink animation
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe-gl/uprobe_gl_sink.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>

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

/** @This is the private structure for gl sink renderer probe. */
struct uprobe_gl_sink {
    /** texture */
    GLuint texture;
    /** SAR */
    struct urational sar;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_gl_sink, uprobe);

/** @internal @This reshapes the gl view upon receiving an Exposure event
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param w window width
 * @param h window height
 */
static void uprobe_gl_sink_reshape(struct uprobe *uprobe,
                                   struct upipe *upipe,
                                   int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(20, w/(float)h, 5, 15);
    glViewport(0, 0, w, h);
    glMatrixMode(GL_MODELVIEW);
}

/** @internal @This updates the probe with latest flow definition
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void uprobe_gl_sink_new_flow(struct uprobe *uprobe,
                                    struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl_sink *uprobe_gl_sink =
        uprobe_gl_sink_from_uprobe(uprobe);

    /* get SAR */
    uprobe_gl_sink->sar.num = uprobe_gl_sink->sar.den = 1;
    uref_pic_flow_get_sar(uref, &uprobe_gl_sink->sar);
    if (unlikely(!uprobe_gl_sink->sar.num || !uprobe_gl_sink->sar.den)) {
        uprobe_gl_sink->sar.num = uprobe_gl_sink->sar.den = 1;
    }
}

/** @internal @This does the actual rendering upon receiving a pic
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return an error code
 */
static int uprobe_gl_sink_render(struct uprobe *uprobe,
                                 struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl_sink *uprobe_gl_sink = uprobe_gl_sink_from_uprobe(uprobe);
    float scale = 1;
    size_t w = 0, h = 0;

    /* load image to texture */

    if (!upipe_gl_texture_load_uref(uref, uprobe_gl_sink->texture)) {
        upipe_err(upipe, "Could not map picture plane");
        return UBASE_ERR_EXTERNAL; 
    }

    uref_pic_size(uref, &w, &h, NULL);
    scale = (uprobe_gl_sink->sar.num * w) /
        ((float) uprobe_gl_sink->sar.den * h);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glBindTexture(GL_TEXTURE_2D, uprobe_gl_sink->texture);
    glLoadIdentity();
    glTranslatef(0, 0, -10);

    // Main movie "display"
    glPushMatrix();
    glScalef(scale, 1, 1);
    glBegin(GL_QUADS);
    {
        glTexCoord2f(0, 1); glVertex3f(-2, -2,  -4);
        glTexCoord2f(0, 0); glVertex3f(-2,  2,  -4);
        glTexCoord2f(1, 0); glVertex3f( 2,  2,  -4);
        glTexCoord2f(1, 1); glVertex3f( 2, -2,  -4);
    }
    glEnd();
    glPopMatrix();

    int ret = uprobe_throw(uprobe->next, upipe, UPROBE_GL_SINK_RENDER,
                           UPIPE_GL_SINK_SIGNATURE, uref);

    glDisable(GL_TEXTURE_2D);
    /* End */

    return ret;
}

/** @internal @This does the gl (window-system non-specific) init
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param w pic width
 * @param h pic height
 */
static void uprobe_gl_sink_init2(struct uprobe *uprobe,
                                 struct upipe *upipe,
                                 int w, int h)
{
    struct uprobe_gl_sink *uprobe_gl_sink =
        uprobe_gl_sink_from_uprobe(uprobe);

    glClearColor (0.0, 0.0, 0.0, 0.0);
    glShadeModel(GL_FLAT);
    glEnable(GL_DEPTH_TEST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &uprobe_gl_sink->texture);
    glBindTexture(GL_TEXTURE_2D, uprobe_gl_sink->texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_gl_sink_throw(struct uprobe *uprobe,
                                struct upipe *upipe,
                                int event, va_list args)
{
    switch (event) {
        case UPROBE_NEW_FLOW_DEF: { /* FIXME */
            struct uref *uref = va_arg(args, struct uref*);
            uprobe_gl_sink_new_flow(uprobe, upipe, uref);
            return UBASE_ERR_NONE;
        }
        case UPROBE_GL_SINK_INIT: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            int w = va_arg(args, int);
            int h = va_arg(args, int);
            uprobe_gl_sink_init2(uprobe, upipe, w, h);
            return UBASE_ERR_NONE;
        }
        case UPROBE_GL_SINK_RENDER: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            return uprobe_gl_sink_render(uprobe, upipe, uref);
        }
        case UPROBE_GL_SINK_RESHAPE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            int w = va_arg(args, int);
            int h = va_arg(args, int);
            uprobe_gl_sink_reshape(uprobe, upipe, w, h);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This initializes a new uprobe_gl_sink structure.
 *
 * @param uprobe_gl_sink pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
static struct uprobe *
uprobe_gl_sink_init(struct uprobe_gl_sink *uprobe_gl_sink,
                    struct uprobe *next)
{
    assert(uprobe_gl_sink != NULL);
    struct uprobe *uprobe = uprobe_gl_sink_to_uprobe(uprobe_gl_sink);

    uprobe_gl_sink->sar.num = uprobe_gl_sink->sar.den = 1;

    uprobe_init(uprobe, uprobe_gl_sink_throw, next);
    return uprobe;
}

/** @internal @This cleans up a uprobe_gl_sink structure.
 *
 * @param uprobe_gl_sink structure to free
 */
static void uprobe_gl_sink_clean(struct uprobe_gl_sink *uprobe_gl_sink)
{
    glDeleteTextures(1, &uprobe_gl_sink->texture);
    struct uprobe *uprobe = &uprobe_gl_sink->uprobe;
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_gl_sink)
#undef ARGS
#undef ARGS_DECL

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
 * @short Upipe GL sink cube animation
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe-gl/uprobe_gl_sink_cube.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_helper_adhoc.h>
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

/** @This is a super-set of the uprobe structure with additional
 * local members. It is an adhoc probe.
 */
struct uprobe_gl_sink_cube {
    /** rotation angle */
    float theta;
    /** texture */
    GLuint texture;
    /** SAR */
    struct urational sar;

    /** pointer to the pipe we're attached to */
    struct upipe *upipe;
    /** structure exported to modules */
    struct uprobe uprobe;
};

static void uprobe_gl_sink_cube_free(struct uprobe *uprobe);

UPROBE_HELPER_UPROBE(uprobe_gl_sink_cube, uprobe);
UPROBE_HELPER_ADHOC(uprobe_gl_sink_cube, upipe);

/** @internal @This reshapes the gl view upon receiving an Exposure event
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param w window width
 * @param h window height
 */
static void uprobe_gl_sink_cube_reshape(struct uprobe *uprobe, struct upipe *upipe, int w, int h)
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
static void uprobe_gl_sink_cube_new_flow(struct uprobe *uprobe,
                                       struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl_sink_cube *cube = uprobe_gl_sink_cube_from_uprobe(uprobe);

    /* get SAR */
    cube->sar.num = cube->sar.den = 1;
    uref_pic_flow_get_sar(uref, &cube->sar);
    if (unlikely(!cube->sar.num || !cube->sar.den)) {
        cube->sar.num = cube->sar.den = 1;
    }
}

/** @internal @This does the actual rendering upon receiving a pic
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void uprobe_gl_sink_cube_render(struct uprobe *uprobe,
                                       struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl_sink_cube *cube = uprobe_gl_sink_cube_from_uprobe(uprobe);
    float scale = 1;
    size_t w = 0, h = 0;

    /* load image to texture */
    if (!upipe_gl_texture_load_uref(uref, cube->texture)) {
        upipe_err(upipe, "Could not map picture plane");
        uref_free(uref);
        return;
    }

    uref_pic_size(uref, &w, &h, NULL);
    scale = (cube->sar.num * w)/((float) cube->sar.den * h);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glBindTexture(GL_TEXTURE_2D, cube->texture);
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

    // Spinning cube
    glPushMatrix();
    glScalef(0.25, 0.25, 0.25);
    glRotatef(30, 1, 0, 0);
    glTranslatef(8, -6, 0);
    glRotatef(cube->theta, 0, 1, 0);
    glBegin(GL_QUADS);
    {
        glTexCoord2f(0, 0); glVertex3f(-1, -1, -1);
        glTexCoord2f(0, 1); glVertex3f(-1, -1,  1);
        glTexCoord2f(1, 1); glVertex3f(-1,  1,  1);
        glTexCoord2f(1, 0); glVertex3f(-1,  1, -1);

        glTexCoord2f(0, 0); glVertex3f( 1, -1, -1);
        glTexCoord2f(0, 1); glVertex3f( 1, -1,  1);
        glTexCoord2f(1, 1); glVertex3f( 1,  1,  1);
        glTexCoord2f(1, 0); glVertex3f( 1,  1, -1);

        glTexCoord2f(0, 0); glVertex3f(-1, -1, -1);
        glTexCoord2f(0, 1); glVertex3f(-1, -1,  1);
        glTexCoord2f(1, 1); glVertex3f( 1, -1,  1);
        glTexCoord2f(1, 0); glVertex3f( 1, -1, -1);

        glTexCoord2f(0, 0); glVertex3f(-1,  1, -1);
        glTexCoord2f(0, 1); glVertex3f(-1,  1,  1);
        glTexCoord2f(1, 1); glVertex3f( 1,  1,  1);
        glTexCoord2f(1, 0); glVertex3f( 1,  1, -1);

        glTexCoord2f(0, 0); glVertex3f(-1, -1, -1);
        glTexCoord2f(0, 1); glVertex3f(-1,  1, -1);
        glTexCoord2f(1, 1); glVertex3f( 1,  1, -1);
        glTexCoord2f(1, 0); glVertex3f( 1, -1, -1);

        glTexCoord2f(0, 0); glVertex3f(-1, -1,  1);
        glTexCoord2f(0, 1); glVertex3f(-1,  1,  1);
        glTexCoord2f(1, 1); glVertex3f( 1,  1,  1);
        glTexCoord2f(1, 0); glVertex3f( 1, -1,  1);
    }
    glEnd();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);

    /* Rotate a bit more */
    cube->theta = cube->theta + 0.4;

    /* End */
}

/** @internal @This does the gl (window-system non-specific) init
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param w pic width
 * @param h pic height
 */
static void uprobe_gl_sink_cube_init(struct uprobe *uprobe, struct upipe *upipe, int w, int h)
{
    struct uprobe_gl_sink_cube *cube = uprobe_gl_sink_cube_from_uprobe(uprobe);

    glClearColor (0.0, 0.0, 0.0, 0.0);
    glShadeModel(GL_FLAT);
    glEnable(GL_DEPTH_TEST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &cube->texture);
    glBindTexture(GL_TEXTURE_2D, cube->texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return true for gl (init, render, reshape)  events
 */
static bool uprobe_gl_sink_cube_throw(struct uprobe *uprobe,
                        struct upipe *upipe, enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *uref = va_arg(args, struct uref*);
            uprobe_gl_sink_cube_new_flow(uprobe, upipe, uref);
            return true;
        }
        case UPROBE_GL_SINK_INIT: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            int w = va_arg(args, int);
            int h = va_arg(args, int);
            uprobe_gl_sink_cube_init(uprobe, upipe, w, h);
            return true;
        }
        case UPROBE_GL_SINK_RENDER: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            uprobe_gl_sink_cube_render(uprobe, upipe, uref);
            return true;
        }
        case UPROBE_GL_SINK_RESHAPE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GL_SINK_SIGNATURE);
            int w = va_arg(args, int);
            int h = va_arg(args, int);
            uprobe_gl_sink_cube_reshape(uprobe, upipe, w, h);
            return true;
        }
        default:
            return uprobe_gl_sink_cube_throw_adhoc(uprobe, upipe, event, args);
    }
}

/** @This frees a uprobe_gl_sink_cube structure.
 *
 * @param uprobe structure to free
 */
static void uprobe_gl_sink_cube_free(struct uprobe *uprobe)
{
    struct uprobe_gl_sink_cube *cube = uprobe_gl_sink_cube_from_uprobe(uprobe);
    glDeleteTextures(1, &cube->texture);
    uprobe_gl_sink_cube_clean_adhoc(uprobe);
    free(cube);
}

/** @This allocates a new uprobe_gl_sink_cube structure in ad-hoc mode (will be
 * deallocated when the pipe dies).
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_gl_sink_cube_alloc(struct uprobe *next)
{
    struct uprobe_gl_sink_cube *cube = malloc(sizeof(struct uprobe_gl_sink_cube));
    if (unlikely(cube == NULL)) {
        uprobe_throw_fatal(next, NULL, UPROBE_ERR_ALLOC);
        return next;
    }
    struct uprobe *uprobe = &cube->uprobe;

    cube->theta = 0;
    cube->sar.num = cube->sar.den = 1;

    uprobe_init(uprobe, uprobe_gl_sink_cube_throw, next);
    uprobe_gl_sink_cube_init_adhoc(uprobe);
    return uprobe;
}



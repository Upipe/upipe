/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe GL sink cube animation
 */

#include "upipe/ubase.h"
#include "upipe/uprobe.h"
#include "upipe-gl/uprobe_gl_sink_cube.h"
#include "upipe/uprobe_helper_uprobe.h"
#include "upipe/uprobe_helper_alloc.h"

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include <GL/gl.h>

/** @This is a super-set of the uprobe structure with additional
 * local members.
 */
struct uprobe_gl_sink_cube {
    /** rotation angle */
    float theta;
    /** texture */
    GLuint texture;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_gl_sink_cube, uprobe);

/** @internal @This does the actual rendering upon receiving a pic
 * @param uprobe description structure of the probe
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void uprobe_gl_sink_cube_render(struct uprobe *uprobe,
                                       struct upipe *upipe, struct uref *uref)
{
    struct uprobe_gl_sink_cube *cube = uprobe_gl_sink_cube_from_uprobe(uprobe);

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

    /* Rotate a bit more */
    cube->theta = cube->theta + 0.4;

    /* End */
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_gl_sink_cube_throw(struct uprobe *uprobe,
                        struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_GL_SINK_RENDER: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_GL_SINK_SIGNATURE)
            struct uref *uref = va_arg(args, struct uref *);
            uprobe_gl_sink_cube_render(uprobe, upipe, uref);
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** @internal @This initializes a new uprobe_gl_sink_cube structure.
 *
 * @param cube pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
static struct uprobe *uprobe_gl_sink_cube_init(struct uprobe_gl_sink_cube *cube,
                                               struct uprobe *next)
{
    assert(cube != NULL);
    struct uprobe *uprobe = &cube->uprobe;

    cube->theta = 0;

    uprobe_init(uprobe, uprobe_gl_sink_cube_throw, next);
    return uprobe;
}

/** @internal @This cleans up a uprobe_gl_sink_cube structure.
 *
 * @param cube structure to free
 */
static void uprobe_gl_sink_cube_clean(struct uprobe_gl_sink_cube *cube)
{
    glDeleteTextures(1, &cube->texture);
    struct uprobe *uprobe = &cube->uprobe;
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_gl_sink_cube)
#undef ARGS
#undef ARGS_DECL

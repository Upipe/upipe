/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe GL - common definitions
 */

#include "upipe/uref_pic.h"
#include "upipe-gl/upipe_gl_sink_common.h"
#include <GL/gl.h>

/** @This loads a uref picture into the specified texture
 * @param uref uref structure describing the picture
 * @param texture GL texture
 * @return false in case of error
 */
bool upipe_gl_texture_load_uref(struct uref *uref, GLuint texture)
{
    const uint8_t *data = NULL;
    bool rgb565 = false;
    size_t width, height, stride;
    uint8_t msize;
    uref_pic_size(uref, &width, &height, NULL);
    if (!ubase_check(uref_pic_plane_read(uref, "r8g8b8", 0, 0, -1, -1,
                                         &data)) ||
        !ubase_check(uref_pic_plane_size(uref, "r8g8b8", &stride,
                                         NULL, NULL, &msize))) {
        if (!ubase_check(uref_pic_plane_read(uref, "r5g6b5", 0, 0, -1, -1,
                                             &data)) ||
            !ubase_check(uref_pic_plane_size(uref, "r5g6b5", &stride,
                                             NULL, NULL, &msize))) {
            return false;
        }
        rgb565 = true;
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / msize);
    glPixelStorei(GL_UNPACK_ALIGNMENT, rgb565 ? 2 : 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
            rgb565 ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE, data);
    uref_pic_plane_unmap(uref, rgb565 ? "r5g6b5" : "r8g8b8", 0, 0, -1, -1);

    return true;
}

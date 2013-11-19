/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

#include <upipe/uref_pic.h>
#include <upipe-gl/upipe_gl_sink_common.h>
#include <GL/gl.h>

/** @This loads a uref picture into the specified texture
 * @param uref uref structure describing the picture
 * @param texture GL texture
 * @return false in case of error
 */
bool upipe_gl_texture_load_uref(struct uref *uref, GLuint texture)
{
    const uint8_t *data = NULL;
    size_t width, height;
    uref_pic_size(uref, &width, &height, NULL);
    if(!uref_pic_plane_read(uref, "r8g8b8", 0, 0, -1, -1, &data)) {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, 
            height, 0, GL_RGB, GL_UNSIGNED_BYTE, 
            data);
    uref_pic_plane_unmap(uref, "r8g8b8", 0, 0, -1, -1);

    return true;
}

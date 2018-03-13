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
 * @short Upipe buffer handling for picture managers
 * This file defines the picture-specific API to access buffers.
 */

#include <upipe/ubuf_pic.h>

#include <stdint.h>
#include <string.h>

/** @This clears (part of) the specified plane, depending on plane type
 * and size (set U/V chroma to 0x80 instead of 0 for instance)
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @param fullrange whether the input is full-range
 * @return an error code
 */
int ubuf_pic_plane_clear(struct ubuf *ubuf, const char *chroma,
                         int hoffset, int voffset, int hsize, int vsize,
                         int fullrange)
{
    size_t stride, width, height;
    uint8_t hsub, vsub, macropixel_size, macropixel;
    uint8_t *buf = NULL;
    bool known = true;
    int j;

    if (!ubuf)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(ubuf_pic_size(ubuf, &width, &height, &macropixel))
    UBASE_RETURN(ubuf_pic_plane_size(ubuf, chroma,
                    &stride, &hsub, &vsub, &macropixel_size))
    UBASE_RETURN(ubuf_pic_plane_write(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize, &buf))

    if (hsize == -1) {
        width -= hoffset;
    } else {
        width = hsize;
    }
    if (vsize == -1) {
        height -= voffset;
    } else {
        height = vsize;
    }

    const size_t memset_width = width*macropixel_size/hsub/macropixel;

#define MATCH(a) (strcmp(chroma, a) == 0)
#define LINELOOP(a) for (a=0; a < height/vsub; a++)

    if (MATCH("y8") || MATCH("y16l") || MATCH("y16b") || MATCH("a8")
     || MATCH("r8g8b8") || MATCH("r8g8b8a8") || MATCH("a8r8g8b8")
     || MATCH("b8g8r8") || MATCH("b8g8r8a8") || MATCH("a8b8g8r8")) {
        LINELOOP(j) {
            memset(buf, fullrange ? 0 : 16, memset_width);
            buf += stride;
        }
    } else if (MATCH("u8") || MATCH("v8")) {
        LINELOOP(j) {
            memset(buf, 0x80, memset_width);
            buf += stride;
        }
    } else if (MATCH("u10l") || MATCH("v10l")) {
        size_t new_width = width/hsub;
        LINELOOP(j) {
            uint16_t *new_buf = (uint16_t*)buf;
            for (int x = 0; x < new_width; x++)
                new_buf[x] = 0x200;
            buf += stride;
        }
    } else if (MATCH("y10l") && macropixel_size == 2) {
        LINELOOP(j) {
            if (fullrange)
                memset(buf, 0, memset_width);
            else for (int i = 0; i < memset_width; i += macropixel_size) {
                buf[i]   = 16 << 2;
                buf[i+1] = 0;
            }
            buf += stride;
        }
    } else if (MATCH("u10y10v10y10u10y10v10y10u10y10v10y10") && fullrange &&
        macropixel_size == 16) {
        /* this pattern repeated 2 times form a full macropixel */
        static const uint8_t pattern[8] = {
            // u y v + 2 bits, le
            0x00, 0x02, 0x00, 0x20,
            // y u y + 2 bits, le
            0x00, 0x00, 0x08, 0x00,
        };
        LINELOOP(j) {
            for (int i = 0; i < memset_width; i += macropixel_size / 2) {
                memcpy(&buf[i], pattern, sizeof(pattern));
            }
            buf += stride;
        }
    } else {
        known = false;
    }

#undef LINELOOP
#undef MATCH
    UBASE_RETURN(ubuf_pic_plane_unmap(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize))
    return known ? UBASE_ERR_NONE : UBASE_ERR_INVALID;
}

/** @This clears (part of) the specified picture, depending on plane type
 * and size (set U/V chroma to 0x80 instead of 0 for instance)
 *
 * @param ubuf pointer to ubuf
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @param fullrange whether the input is full-range
 * @return an error code
 */
int ubuf_pic_clear(struct ubuf *ubuf, int hoffset, int voffset,
                   int hsize, int vsize, int fullrange)
{
    if (!ubuf)
        return UBASE_ERR_INVALID;

    bool ret = false;
    const char *chroma;
    ubuf_pic_foreach_plane(ubuf, chroma) {
        ret = !ubase_check(ubuf_pic_plane_clear(ubuf, chroma,
            hoffset, voffset, hsize, vsize, fullrange)) || ret;
    }

    return ret ? UBASE_ERR_INVALID : UBASE_ERR_NONE;
}

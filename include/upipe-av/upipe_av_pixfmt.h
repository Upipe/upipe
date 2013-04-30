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
 * @short upipe/avutil pixelformat conversion
 */

#include <upipe/ubuf_pic.h>
#include <libavutil/avutil.h>

/** plane definition */
struct upipe_av_plane {
    /** chroma name */
    const char *chroma;
    /** horizontal subsampling */
    uint8_t hsub;
    /** vertical subsampling */
    uint8_t vsub;
    /** macropixel size */
    uint8_t macropixel_size;
};

/** format definition */
struct upipe_av_pixfmt {
    /** avutil pixelformat */
    enum PixelFormat pixfmt;
    /** planes */
    struct upipe_av_plane planes[4];
};

/** @internal @This is the upipe/avutil pixelformat array
 */
static const struct upipe_av_pixfmt upipe_av_pixfmt[] = {
    {PIX_FMT_YUV420P, {
        {"y8", 1, 1, 1},
        {"u8", 2, 2, 1},
        {"v8", 2, 2, 1},
        {NULL, 0, 0, 0}
    }},
    {PIX_FMT_RGB24, {
        {"rgb24", 1, 1, 3},
        {NULL, 0, 0, 0}
    }},

    {PIX_FMT_NONE, {}}
};

/** @This finds the upipe_av_pixfmt structure corresponding to a picture ubuf
 * @param ubuf picture ubuf
 * @return pointer to upipe_av_pixfmt description structure
 */
static inline const struct upipe_av_pixfmt *upipe_av_pixfmt_from_ubuf(struct ubuf *ubuf)
{
    const struct upipe_av_pixfmt *pixfmt = NULL;
    const struct upipe_av_plane *plane = NULL;
    uint8_t hsub, vsub, macropixel;
    int i;
    
    /* iterate through known formats */
    for (pixfmt = upipe_av_pixfmt; pixfmt->pixfmt != PIX_FMT_NONE; pixfmt++) {
        /* iterate through knwon planes */
        for (i = 0, plane = pixfmt->planes;
                i < 4
                && plane->chroma
                && ubuf_pic_plane_size(ubuf, plane->chroma, NULL, &hsub, &vsub, &macropixel)
                && hsub == plane->hsub
                && vsub == plane->vsub
                && macropixel == plane->macropixel_size;
            plane++, i++);

        /* if plane->chroma == NULL, every known plane has been found */
        if (!plane->chroma) {
            break;
        }
    }
    
    /* if pixfmt->pixfmt != NONE, current format is suitable */
    if (pixfmt->pixfmt != PIX_FMT_NONE) {
        return pixfmt;
    }

    /* unknown format */
    return NULL;
}

/** @This finds the first upipe_av_pixfmt structure corresponding to a PixelFormat
 * @param format avutil pixel format
 * @returns pointer to upipe_av_pixfmt description structure
 */
static inline const struct upipe_av_pixfmt *upipe_av_pixfmt_from_pixfmt(enum PixelFormat format) {
    const struct upipe_av_pixfmt *pixfmt = NULL;
    for (pixfmt = upipe_av_pixfmt;
         pixfmt->pixfmt != PIX_FMT_NONE && pixfmt->pixfmt != format;
         pixfmt++);
    if (pixfmt->pixfmt == format) {
        return pixfmt;
    }
    return NULL;
}


/** @This finds the upipe_av_pixfmt structure corresponding to a picture ubuf manager.
 * It works by allocating a tiny picture ubuf.
 * @param ubuf_mgr picture ubuf manager
 * @return pointer to upipe_av_pixfmt description structure
 */
static inline const struct upipe_av_pixfmt *upipe_av_pixfmt_from_ubuf_mgr(struct ubuf_mgr *mgr)
{
    struct ubuf *ubuf;
    const struct upipe_av_pixfmt *pixfmt = NULL;

    /* allocate small picture */
    ubuf = ubuf_pic_alloc(mgr, 4, 4);
    if (unlikely(!ubuf)) {
        return NULL;
    }

    pixfmt = upipe_av_pixfmt_from_ubuf(ubuf);
    ubuf_free(ubuf);

    return pixfmt;
}


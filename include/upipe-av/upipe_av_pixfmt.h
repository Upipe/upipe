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

#ifndef _UPIPE_AV_UPIPE_AV_PIXFMT_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_PIXFMT_H_

#include <upipe/ubuf_pic.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <string.h>

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
    enum PixelFormat pixfmt[PIX_FMT_NB];
    /** planes */
    struct upipe_av_plane planes[4];
};

/** @internal @This is the upipe/avutil pixelformat array
 */
static const struct upipe_av_pixfmt upipe_av_pixfmt[] = {
    {{PIX_FMT_YUV420P, PIX_FMT_YUVJ420P, PIX_FMT_NONE}, {
        {"y8", 1, 1, 1},
        {"u8", 2, 2, 1},
        {"v8", 2, 2, 1},
        {NULL, 0, 0, 0}
    }},
    {{PIX_FMT_RGB24, PIX_FMT_NONE}, {
        {"rgb24", 1, 1, 3},
        {NULL, 0, 0, 0}
    }},

    {{PIX_FMT_NONE}, {}}
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
    for (pixfmt = upipe_av_pixfmt; *pixfmt->pixfmt != PIX_FMT_NONE; pixfmt++) {
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
    if (*pixfmt->pixfmt != PIX_FMT_NONE) {
        return pixfmt;
    }

    /* unknown format */
    return NULL;
}

/** @This finds the first upipe_av_pixfmt structure corresponding to a PixelFormat
 * @param format avutil pixel format
 * @returns pointer to upipe_av_pixfmt description structure
 */
static inline const struct upipe_av_pixfmt *upipe_av_pixfmt_from_pixfmt(enum PixelFormat format)
{
    const struct upipe_av_pixfmt *pixfmt = NULL;
    for (pixfmt = upipe_av_pixfmt;
         *pixfmt->pixfmt != PIX_FMT_NONE && *pixfmt->pixfmt != format;
         pixfmt++);
    if (*pixfmt->pixfmt == format) {
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

/** @This returns the first PixelFormat in fmts_pref matching a
 * format in fmts
 * @param fmts_pref sorted preferred formats
 * @param available available formats
 */
static inline enum PixelFormat upipe_av_pixfmt_best(const enum PixelFormat *fmts_pref,
                                                    const enum PixelFormat *fmts)
{
    int i;
    while (*fmts_pref != PIX_FMT_NONE) {
        for (i=0; fmts[i] != PIX_FMT_NONE
                && fmts[i] != *fmts_pref; i++);
        if (fmts[i] != PIX_FMT_NONE) {
            break;
        }
        fmts_pref++;
    }
    return *fmts_pref;
}

/** @This clears the given (sub)picture to obtain a black area
 * @param ubuf picture ubuf
 * @param fmt pointer to upipe_av_pixfmt description structure
 */
static inline void upipe_av_pixfmt_clear_picture(struct ubuf *ubuf,
                    int hoffset, int voffset, int hsize, int vsize,
                                 const struct upipe_av_pixfmt *fmt)
{
    int i, j;
    size_t stride, width, height;
    uint8_t val, hsub, vsub, macropixel, *buf;
    const struct upipe_av_plane *plane = NULL;

    if (unlikely(!ubuf)) {
        return;
    }
    if (unlikely(*fmt->pixfmt <= PIX_FMT_NONE || *fmt->pixfmt >= PIX_FMT_NB)) {
        return;
    }
    const AVPixFmtDescriptor *desc = av_pix_fmt_descriptors + *fmt->pixfmt;

    ubuf_pic_size(ubuf, &width, &height, NULL);
    if (hsize <= 0) { 
        hsize = width;
    }
    if (vsize <= 0) { 
        vsize = height;
    }
    
    plane = fmt->planes;
    for (i=0; i < 4 && plane[i].chroma; i++) {
        ubuf_pic_plane_write(ubuf, plane[i].chroma,
                             hoffset, voffset, hsize, vsize, &buf);
        ubuf_pic_plane_size(ubuf, plane[i].chroma,
                            &stride, &hsub, &vsub, &macropixel);
        val = 0;
        if (i > 0 && !(desc->flags & PIX_FMT_RGB)) {
            val = 0x80; /* UV value for black pixel */
        }
        for (j=0; j < vsize/vsub; j++) {
            memset(buf, val, macropixel*hsize/hsub);
            buf += stride;
        }
        ubuf_pic_plane_unmap(ubuf, plane[i].chroma, 0, 0, -1, -1);
    }
}

#endif

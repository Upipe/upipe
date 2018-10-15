/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * This is also used in swscale.
 */

#ifndef _UPIPE_AV_UPIPE_AV_PIXFMT_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_PIXFMT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubuf_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic_flow_formats.h>

#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>

#include <string.h>

/** maximum number of planes + 1 in supported pixel formats */
#define UPIPE_AV_MAX_PLANES 5

/** @This returns the upipe flow format for a given avutil pixel format.
 *
 * @param pix_fmt avutil pixel format
 * @return the upipe flow format description or NULL
 */
static inline const struct uref_pic_flow_format *
upipe_av_pixfmt_to_format(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
        case AV_PIX_FMT_YUVA420P:
            return &uref_pic_flow_format_yuva420p;
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            return &uref_pic_flow_format_yuv420p;
        case AV_PIX_FMT_YUVA422P:
            return &uref_pic_flow_format_yuva422p;
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUVJ422P:
            return &uref_pic_flow_format_yuv422p;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUVJ444P:
            return &uref_pic_flow_format_yuv444p;
        case AV_PIX_FMT_YUYV422:
            return &uref_pic_flow_format_yuyv422;
        case AV_PIX_FMT_UYVY422:
            return &uref_pic_flow_format_uyvy422;
        case AV_PIX_FMT_YUV420P10LE:
            return &uref_pic_flow_format_yuv420p10le;
        case AV_PIX_FMT_YUV420P10BE:
            return &uref_pic_flow_format_yuv420p10be;
        case AV_PIX_FMT_YUV422P10LE:
            return &uref_pic_flow_format_yuv422p10le;
        case AV_PIX_FMT_YUV422P10BE:
            return &uref_pic_flow_format_yuv422p10be;
        case AV_PIX_FMT_YUV444P10LE:
            return &uref_pic_flow_format_yuv444p10le;
        case AV_PIX_FMT_YUV444P10BE:
            return &uref_pic_flow_format_yuv444p10be;
        case AV_PIX_FMT_YUV420P12LE:
            return &uref_pic_flow_format_yuv420p12le;
        case AV_PIX_FMT_YUV420P12BE:
            return &uref_pic_flow_format_yuv420p12be;
        case AV_PIX_FMT_YUV422P12LE:
            return &uref_pic_flow_format_yuv422p12le;
        case AV_PIX_FMT_YUV422P12BE:
            return &uref_pic_flow_format_yuv422p12be;
        case AV_PIX_FMT_YUV444P12LE:
            return &uref_pic_flow_format_yuv444p12le;
        case AV_PIX_FMT_YUV444P12BE:
            return &uref_pic_flow_format_yuv444p12be;
        case AV_PIX_FMT_YUV420P16LE:
            return &uref_pic_flow_format_yuv420p16le;
        case AV_PIX_FMT_YUV420P16BE:
            return &uref_pic_flow_format_yuv420p16be;
        case AV_PIX_FMT_YUV422P16LE:
            return &uref_pic_flow_format_yuv422p16le;
        case AV_PIX_FMT_YUV422P16BE:
            return &uref_pic_flow_format_yuv422p16be;
        case AV_PIX_FMT_YUV444P16LE:
            return &uref_pic_flow_format_yuv444p16le;
        case AV_PIX_FMT_YUV444P16BE:
            return &uref_pic_flow_format_yuv444p16be;
        case AV_PIX_FMT_GRAY8:
            return &uref_pic_flow_format_gray8;
        case AV_PIX_FMT_RGB565:
            return &uref_pic_flow_format_rgb565;
        case AV_PIX_FMT_RGB24:
            return &uref_pic_flow_format_rgb24;
        case AV_PIX_FMT_BGR24:
            return &uref_pic_flow_format_bgr24;
        case AV_PIX_FMT_ARGB:
            return &uref_pic_flow_format_argb;
        case AV_PIX_FMT_RGBA:
            return &uref_pic_flow_format_rgba;
        case AV_PIX_FMT_ABGR:
            return &uref_pic_flow_format_abgr;
        case AV_PIX_FMT_BGRA:
            return &uref_pic_flow_format_bgra;
        case AV_PIX_FMT_RGBA64BE:
            return &uref_pic_flow_format_rgba64be;
        default:
            break;
    }
    return NULL;
}

/** @This configures the flow definition according to the given pixel format.
 *
 * @param pix_fmt avcodec pixel format
 * @param flow_def overwritten flow definition
 * @return an error code
 */
static inline int upipe_av_pixfmt_to_flow_def(enum AVPixelFormat pix_fmt,
                                              struct uref *flow_def)
{
    const struct uref_pic_flow_format *fmt = upipe_av_pixfmt_to_format(pix_fmt);
    if (unlikely(!fmt))
        return UBASE_ERR_UNHANDLED;

    UBASE_RETURN(uref_pic_flow_set_format(flow_def, fmt));
    return uref_flow_set_def(flow_def, UREF_PIC_FLOW_DEF);
}

/** @This finds the appropriate av pixel format according to the flow
 * definition, and creates a mapping system for planes.
 *
 * @param flow_def flow definition
 * @param pix_fmts allowed pixel formats, terminated by -1 (or NULL for any)
 * @param chroma_map av plane number vs. chroma map
 * @return selected pixel format, or AV_PIX_FMT_NONE if no compatible pixel format
 * was found
 */
static inline enum AVPixelFormat
    upipe_av_pixfmt_from_flow_def(struct uref *flow_def,
                                  const enum AVPixelFormat *pix_fmts,
                                  const char *chroma_p[UPIPE_AV_MAX_PLANES])
{
    static const enum AVPixelFormat supported_fmts[] = {
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_UYVY422,
        AV_PIX_FMT_YUV420P10LE,
        AV_PIX_FMT_YUV420P10BE,
        AV_PIX_FMT_YUV420P12LE,
        AV_PIX_FMT_YUV420P12BE,
        AV_PIX_FMT_YUV420P16LE,
        AV_PIX_FMT_YUV420P16BE,
        AV_PIX_FMT_YUV422P10LE,
        AV_PIX_FMT_YUV422P10BE,
        AV_PIX_FMT_YUV422P12LE,
        AV_PIX_FMT_YUV422P12BE,
        AV_PIX_FMT_YUV422P16LE,
        AV_PIX_FMT_YUV422P16BE,
        AV_PIX_FMT_YUV444P10LE,
        AV_PIX_FMT_YUV444P10BE,
        AV_PIX_FMT_YUV444P12LE,
        AV_PIX_FMT_YUV444P12BE,
        AV_PIX_FMT_YUV444P16LE,
        AV_PIX_FMT_YUV444P16BE,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_RGB565,
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGBA64BE,
        -1
    };
    if (pix_fmts == NULL)
        pix_fmts = supported_fmts;

    uint8_t macropixel;
    uint8_t nb_planes;
    if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
        !ubase_check(uref_pic_flow_get_planes(flow_def, &nb_planes)) ||
        nb_planes >= UPIPE_AV_MAX_PLANES)
        return -1;

    const struct uref_pic_flow_format *fmt = NULL;

    while (*pix_fmts != -1) {
        const struct uref_pic_flow_format *fmt =
            upipe_av_pixfmt_to_format(*pix_fmts);

        if (fmt && ubase_check(uref_pic_flow_check_format(flow_def, fmt))) {
            for (uint8_t i = 0; i < fmt->nb_planes; i++)
                chroma_p[i] = fmt->planes[i].chroma;
            chroma_p[fmt->nb_planes] = NULL;
            return *pix_fmts;
        }
        pix_fmts++;
    }

    return AV_PIX_FMT_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

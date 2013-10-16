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

#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>

#include <string.h>

/** maximum number of planes + 1 in supported pixel formats */
#define UPIPE_AV_MAX_PLANES 5

/** @This configures the flow definition according to the given pixel format.
 *
 * @param pix_fmt avcodec pixel format
 * @param flow_def overwritten flow definition
 * @return false in case of error
 */
static inline bool upipe_av_pixfmt_to_flow_def(enum PixelFormat pix_fmt,
                                               struct uref *flow_def)
{
    bool ret = true;

    ret = ret && uref_pic_flow_set_planes(flow_def, 0);
    switch (pix_fmt) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8");
            break;
        case AV_PIX_FMT_YUYV422:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 2);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 4, "y8u8y8v8");
            break;
        case AV_PIX_FMT_UYVY422:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 2);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 4, "u8y8v8y8");
            break;
        case AV_PIX_FMT_YUV420P16LE:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16l");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 2, 2, "u16l");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 2, 2, "v16l");
            break;
        case AV_PIX_FMT_YUV420P16BE:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16b");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 2, 2, "u16b");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 2, 2, "v16b");
            break;
        case AV_PIX_FMT_YUV422P16LE:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16l");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 1, 2, "u16l");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 1, 2, "v16l");
            break;
        case AV_PIX_FMT_YUV422P16BE:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16b");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 1, 2, "u16b");
            ret = ret && uref_pic_flow_add_plane(flow_def, 2, 1, 2, "v16b");
            break;
        case AV_PIX_FMT_YUV444P16LE:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16l");
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "u16l");
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "v16l");
            break;
        case AV_PIX_FMT_YUV444P16BE:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16b");
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "u16b");
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 2, "v16b");
            break;
        case AV_PIX_FMT_RGB24:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 3, "r8g8b8");
            break;
        case AV_PIX_FMT_BGR24:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 3, "b8g8r8");
            break;
        case AV_PIX_FMT_ARGB:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 4, "a8r8g8b8");
            break;
        case AV_PIX_FMT_RGBA:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 4, "r8g8b8a8");
            break;
        case AV_PIX_FMT_ABGR:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 4, "a8b8g8r8");
            break;
        case AV_PIX_FMT_BGRA:
            ret = ret && uref_pic_flow_set_macropixel(flow_def, 1);
            ret = ret && uref_pic_flow_add_plane(flow_def, 1, 1, 4, "b8g8r8a8");
            break;
        default:
            return false;
    }

    ret = ret && uref_flow_set_def(flow_def, UREF_PIC_FLOW_DEF);
    return ret;
}

/** @This finds the appropriate av pixel format according to the flow
 * definition, and creates a mapping system for planes.
 *
 * @param flow_def flow definition
 * @param pix_fmts allowed pixel formats, terminated by -1 (or NULL for any)
 * @param chroma_map av plane number vs. chroma map
 * @return selected pixel format, or PIX_FMT_NONE if no compatible pixel format
 * was found
 */
static inline enum PixelFormat
    upipe_av_pixfmt_from_flow_def(struct uref *flow_def,
                                  const enum PixelFormat *pix_fmts,
                                  const char *chroma_p[UPIPE_AV_MAX_PLANES])
{
    static const enum PixelFormat supported_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUYV422,
        AV_PIX_FMT_UYVY422,
        AV_PIX_FMT_YUV420P16LE,
        AV_PIX_FMT_YUV420P16BE,
        AV_PIX_FMT_YUV422P16LE,
        AV_PIX_FMT_YUV422P16BE,
        AV_PIX_FMT_YUV444P16LE,
        AV_PIX_FMT_YUV444P16BE,
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_ABGR,
        AV_PIX_FMT_BGRA,
        -1
    };
    if (pix_fmts == NULL)
        pix_fmts = supported_fmts;

    uint8_t macropixel;
    if (!uref_pic_flow_get_macropixel(flow_def, &macropixel))
        return -1;

    while (*pix_fmts != -1) {
        switch (*pix_fmts) {
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "u8") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "v8")) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = "u8";
                    chroma_p[2] = "v8";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUYV422:
                if (macropixel == 2 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "y8u8y8v8")) {
                    chroma_p[0] = "y8u8y8v8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_UYVY422:
                if (macropixel == 2 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "u8y8v8y8")) {
                    chroma_p[0] = "u8y8v8y8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P16LE:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16l") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "u16l") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "v16l")) {
                    chroma_p[0] = "y16l";
                    chroma_p[1] = "u16l";
                    chroma_p[2] = "v16l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P16BE:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16b") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "u16b") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "v16b")) {
                    chroma_p[0] = "y16b";
                    chroma_p[1] = "u16b";
                    chroma_p[2] = "v16b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P16LE:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16l") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u16l") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v16l")) {
                    chroma_p[0] = "y16l";
                    chroma_p[1] = "u16l";
                    chroma_p[2] = "v16l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P16BE:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16b") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u16b") &&
                    uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v16b")) {
                    chroma_p[0] = "y16b";
                    chroma_p[1] = "u16b";
                    chroma_p[2] = "v16b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P16LE:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16l") &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "u16l") &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "v16l")) {
                    chroma_p[0] = "y16l";
                    chroma_p[1] = "u16l";
                    chroma_p[2] = "v16l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P16BE:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16b") &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "u16b") &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "v16b")) {
                    chroma_p[0] = "y16b";
                    chroma_p[1] = "u16b";
                    chroma_p[2] = "v16b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_RGB24:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 3, "r8g8b8")) {
                    chroma_p[0] = "r8g8b8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_BGR24:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 3, "b8g8r8")) {
                    chroma_p[0] = "b8g8r8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_ARGB:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "a8r8g8b8")) {
                    chroma_p[0] = "a8r8g8b8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_RGBA:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "r8g8b8a8")) {
                    chroma_p[0] = "r8g8b8a8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_ABGR:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "a8b8g8r8")) {
                    chroma_p[0] = "a8b8g8r8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_BGRA:
                if (macropixel == 1 &&
                    uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "b8g8r8a8")) {
                    chroma_p[0] = "b8g8r8a8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            default:
                return false;
        }
        pix_fmts++;
    }

    return AV_PIX_FMT_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

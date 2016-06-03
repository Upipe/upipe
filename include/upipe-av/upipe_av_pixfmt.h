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
 * @return an error code
 */
static inline int upipe_av_pixfmt_to_flow_def(enum AVPixelFormat pix_fmt,
                                              struct uref *flow_def)
{
    UBASE_RETURN(uref_pic_flow_set_planes(flow_def, 0))
    switch (pix_fmt) {
        case AV_PIX_FMT_YUVA420P:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "a8"))
            break;
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"))
            break;
        case AV_PIX_FMT_YUVA422P:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 1, "u8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 1, "v8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "a8"))
            break;
        case AV_PIX_FMT_YUV422P:
        case AV_PIX_FMT_YUVJ422P:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 1, "u8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 1, "v8"))
            break;
        case AV_PIX_FMT_YUV444P:
        case AV_PIX_FMT_YUVJ444P:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "u8"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "v8"))
            break;
        case AV_PIX_FMT_YUYV422:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 2))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "y8u8y8v8"))
            break;
        case AV_PIX_FMT_UYVY422:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 2))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "u8y8v8y8"))
            break;
        case AV_PIX_FMT_YUV420P10LE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "u10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "v10l"))
            break;
        case AV_PIX_FMT_YUV420P10BE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y10b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "u10b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "v10b"))
            break;
        case AV_PIX_FMT_YUV422P10LE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "u10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "v10l"))
            break;
        case AV_PIX_FMT_YUV422P10BE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y10b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "u10b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "v10b"))
            break;
        case AV_PIX_FMT_YUV444P10LE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "u10l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "v10l"))
            break;
        case AV_PIX_FMT_YUV444P10BE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y10b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "u10b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "v10b"))
            break;
        case AV_PIX_FMT_YUV420P16LE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "u16l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "v16l"))
            break;
        case AV_PIX_FMT_YUV420P16BE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "u16b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 2, 2, "v16b"))
            break;
        case AV_PIX_FMT_YUV422P16LE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "u16l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "v16l"))
            break;
        case AV_PIX_FMT_YUV422P16BE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "u16b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 2, 1, 2, "v16b"))
            break;
        case AV_PIX_FMT_YUV444P16LE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "u16l"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "v16l"))
            break;
        case AV_PIX_FMT_YUV444P16BE:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "y16b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "u16b"))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 2, "v16b"))
            break;
        case AV_PIX_FMT_GRAY8:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"))
            break;
        case AV_PIX_FMT_RGB24:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 3, "r8g8b8"))
            break;
        case AV_PIX_FMT_BGR24:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 3, "b8g8r8"))
            break;
        case AV_PIX_FMT_ARGB:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "a8r8g8b8"))
            break;
        case AV_PIX_FMT_RGBA:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "r8g8b8a8"))
            break;
        case AV_PIX_FMT_ABGR:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "a8b8g8r8"))
            break;
        case AV_PIX_FMT_BGRA:
            UBASE_RETURN(uref_pic_flow_set_macropixel(flow_def, 1))
            UBASE_RETURN(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "b8g8r8a8"))
            break;
        default:
            return UBASE_ERR_UNHANDLED;
    }

    UBASE_RETURN(uref_flow_set_def(flow_def, UREF_PIC_FLOW_DEF))
    return UBASE_ERR_NONE;
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
        AV_PIX_FMT_YUV420P16LE,
        AV_PIX_FMT_YUV420P16BE,
        AV_PIX_FMT_YUV422P10LE,
        AV_PIX_FMT_YUV422P10BE,
        AV_PIX_FMT_YUV422P16LE,
        AV_PIX_FMT_YUV422P16BE,
        AV_PIX_FMT_YUV444P10LE,
        AV_PIX_FMT_YUV444P10BE,
        AV_PIX_FMT_YUV444P16LE,
        AV_PIX_FMT_YUV444P16BE,
        AV_PIX_FMT_GRAY8,
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
    uint8_t nb_planes;
    if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
        !ubase_check(uref_pic_flow_get_planes(flow_def, &nb_planes)))
        return -1;

#define u ubase_check
    while (*pix_fmts != -1) {
        switch (*pix_fmts) {
            case AV_PIX_FMT_YUVA420P:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "u8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "v8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "a8"))) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = "u8";
                    chroma_p[2] = "v8";
                    chroma_p[3] = "a8";
                    chroma_p[4] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "u8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "v8"))) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = "u8";
                    chroma_p[2] = "v8";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUVA422P:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "u8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "v8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "a8"))) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = "u8";
                    chroma_p[2] = "v8";
                    chroma_p[3] = "a8";
                    chroma_p[4] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P:
            case AV_PIX_FMT_YUVJ422P:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "u8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "v8"))) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = "u8";
                    chroma_p[2] = "v8";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P:
            case AV_PIX_FMT_YUVJ444P:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "u8")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "v8"))) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = "u8";
                    chroma_p[2] = "v8";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUYV422:
                if (macropixel == 2 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "y8u8y8v8"))) {
                    chroma_p[0] = "y8u8y8v8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_UYVY422:
                if (macropixel == 2 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "u8y8v8y8"))) {
                    chroma_p[0] = "u8y8v8y8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P10LE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "u10l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "v10l"))) {
                    chroma_p[0] = "y10l";
                    chroma_p[1] = "u10l";
                    chroma_p[2] = "v10l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P10BE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "u10b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "v10b"))) {
                    chroma_p[0] = "y10b";
                    chroma_p[1] = "u10b";
                    chroma_p[2] = "v10b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P16LE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "u16l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "v16l"))) {
                    chroma_p[0] = "y16l";
                    chroma_p[1] = "u16l";
                    chroma_p[2] = "v16l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV420P16BE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "u16b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 2, 2, "v16b"))) {
                    chroma_p[0] = "y16b";
                    chroma_p[1] = "u16b";
                    chroma_p[2] = "v16b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P10LE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u10l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v10l"))) {
                    chroma_p[0] = "y10l";
                    chroma_p[1] = "u10l";
                    chroma_p[2] = "v10l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P10BE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u10b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v10b"))) {
                    chroma_p[0] = "y10b";
                    chroma_p[1] = "u10b";
                    chroma_p[2] = "v10b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P16LE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u16l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v16l"))) {
                    chroma_p[0] = "y16l";
                    chroma_p[1] = "u16l";
                    chroma_p[2] = "v16l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV422P16BE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "u16b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 2, 1, 2, "v16b"))) {
                    chroma_p[0] = "y16b";
                    chroma_p[1] = "u16b";
                    chroma_p[2] = "v16b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P10LE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "u10l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "v10l"))) {
                    chroma_p[0] = "y10l";
                    chroma_p[1] = "u10l";
                    chroma_p[2] = "v10l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P10BE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y10b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "u10b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "v10b"))) {
                    chroma_p[0] = "y10b";
                    chroma_p[1] = "u10b";
                    chroma_p[2] = "v10b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P16LE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "u16l")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "v16l"))) {
                    chroma_p[0] = "y16l";
                    chroma_p[1] = "u16l";
                    chroma_p[2] = "v16l";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_YUV444P16BE:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "y16b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "u16b")) &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 2, "v16b"))) {
                    chroma_p[0] = "y16b";
                    chroma_p[1] = "u16b";
                    chroma_p[2] = "v16b";
                    chroma_p[3] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_GRAY8:
                if (macropixel == 1 && nb_planes == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8"))) {
                    chroma_p[0] = "y8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_RGB24:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 3, "r8g8b8"))) {
                    chroma_p[0] = "r8g8b8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_BGR24:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 3, "b8g8r8"))) {
                    chroma_p[0] = "b8g8r8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_ARGB:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "a8r8g8b8"))) {
                    chroma_p[0] = "a8r8g8b8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_RGBA:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "r8g8b8a8"))) {
                    chroma_p[0] = "r8g8b8a8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_ABGR:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "a8b8g8r8"))) {
                    chroma_p[0] = "a8b8g8r8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            case AV_PIX_FMT_BGRA:
                if (macropixel == 1 &&
                    u(uref_pic_flow_check_chroma(flow_def, 1, 1, 4, "b8g8r8a8"))) {
                    chroma_p[0] = "b8g8r8a8";
                    chroma_p[1] = NULL;
                    return *pix_fmts;
                }
                break;
            default:
                return -1;
        }
        pix_fmts++;
    }
#undef u

    return AV_PIX_FMT_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

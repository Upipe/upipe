/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe picture flow format definitions and helpers.
 */

#ifndef _UPIPE_UREF_PIC_FLOW_FORMATS_H_
/** @hidden */
#define _UPIPE_UREF_PIC_FLOW_FORMATS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_pic_flow.h>

/** @This describes a picture format. */
struct uref_pic_flow_format {
    /** macro pixel */
    uint8_t macropixel;
    /** number of planes */
    uint8_t nb_planes;
    /** array of plane descriptions */
    struct {
        /** horizontal subsampling */
        uint8_t hsub;
        /** vertical subsampling */
        uint8_t vsub;
        /** size in octets of a compound */
        uint8_t mpixel_size;
        /** chroma type */
        const char *chroma;
        /** size in bits of a compound */
        uint8_t mpixel_bits;
    } planes[];
};

/** @This checks a flow format.
 *
 * @param uref uref control packet
 * @param format picture format to check
 * @return an error code
 */
static inline int
uref_pic_flow_check_format(struct uref *uref,
                           const struct uref_pic_flow_format *format)
{
    uint8_t macropixel;
    uint8_t planes;
    UBASE_RETURN(uref_pic_flow_get_macropixel(uref, &macropixel));
    UBASE_RETURN(uref_pic_flow_get_planes(uref, &planes));
    if (macropixel != format->macropixel || planes != format->nb_planes)
        return UBASE_ERR_INVALID;
    for (size_t i = 0; i < format->nb_planes; i++) {
        UBASE_RETURN(uref_pic_flow_check_chroma(
                uref,
                format->planes[i].hsub,
                format->planes[i].vsub,
                format->planes[i].mpixel_size,
                format->planes[i].chroma));
    }
    return UBASE_ERR_NONE;
}

/** @This sets a flow format.
 *
 * @param uref uref control packet
 * @param format picture format to set
 * @return an error code
 */
static inline int
uref_pic_flow_set_format(struct uref *uref,
                         const struct uref_pic_flow_format *format)
{
    uref_pic_flow_clear_format(uref);
    UBASE_RETURN(uref_pic_flow_set_macropixel(uref, format->macropixel));
    UBASE_RETURN(uref_pic_flow_set_planes(uref, 0));
    for (size_t i = 0; i < format->nb_planes; i++) {
        UBASE_RETURN(uref_pic_flow_add_plane(
                uref,
                format->planes[i].hsub,
                format->planes[i].vsub,
                format->planes[i].mpixel_size,
                format->planes[i].chroma));
    }
    return UBASE_ERR_NONE;
}


/** @This allocates a control packet to define a new picture flow
 * @see uref_pic_flow_alloc_def, and registers the planes according to the
 * format.
 *
 * @param mgr uref management structure
 * @param format desired format to configure
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *
uref_pic_flow_alloc_format(struct uref_mgr *mgr,
                           const struct uref_pic_flow_format *format)
{
    struct uref *uref = uref_pic_flow_alloc_def(mgr, format->macropixel);
    if (unlikely(!uref))
        return NULL;

    for (size_t i = 0; i < format->nb_planes; i++) {
        if (unlikely(!ubase_check(uref_pic_flow_add_plane(
                        uref,
                        format->planes[i].hsub,
                        format->planes[i].vsub,
                        format->planes[i].mpixel_size,
                        format->planes[i].chroma)))) {
            uref_free(uref);
            return NULL;
        }
    }
    return uref;
}

/** @This defines a helper functions to deal with a specified format.
 *
 * @param Format format to check
 */
#define UREF_PIC_FLOW_FORMAT_HELPER(Format)                                 \
                                                                            \
/** @This checks a flow format.                                             \
 *                                                                          \
 * @param uref uref control packet                                          \
 * @return an error code                                                    \
 */                                                                         \
static inline int uref_pic_flow_check_##Format(struct uref *flow_def)       \
{                                                                           \
    return uref_pic_flow_check_format(                                      \
        flow_def, &uref_pic_flow_format_##Format);                          \
}                                                                           \
                                                                            \
static inline int uref_pic_flow_set_##Format(struct uref *flow_def)         \
{                                                                           \
    return uref_pic_flow_set_format(                                        \
        flow_def, &uref_pic_flow_format_##Format);                          \
}                                                                           \
                                                                            \
/** @This allocates a control packet to define a format.                    \
 * @param mgr uref management structure                                     \
 * @return pointer to uref or NULL in case of failure                       \
 */                                                                         \
static inline struct uref *                                                 \
uref_pic_flow_alloc_##Format(struct uref_mgr *mgr)                          \
{                                                                           \
    return uref_pic_flow_alloc_format(mgr, &uref_pic_flow_format_##Format); \
}

/** @This is the description of the yuva420p format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuva420p = {
    .macropixel = 1,
    .nb_planes = 4,
    .planes = {
        { 1, 1, 1, "y8", 8 },
        { 2, 2, 1, "u8", 8 },
        { 2, 2, 1, "v8", 8 },
        { 1, 1, 1, "a8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuva420p);

/** @This is the description of the yuva422p format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuva422p = {
    .macropixel = 1,
    .nb_planes = 4,
    .planes = {
        { 1, 1, 1, "y8", 8 },
        { 2, 1, 1, "u8", 8 },
        { 2, 1, 1, "v8", 8 },
        { 1, 1, 1, "a8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuva422p);

/** @This is the description of the yuva444p format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuva444p = {
    .macropixel = 1,
    .nb_planes = 4,
    .planes = {
        { 1, 1, 1, "y8", 8 },
        { 1, 1, 1, "u8", 8 },
        { 1, 1, 1, "v8", 8 },
        { 1, 1, 1, "a8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuva444p);

/** @This is the description of the yuv420p format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 1, "y8", 8 },
        { 2, 2, 1, "u8", 8 },
        { 2, 2, 1, "v8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p);

/** @This is the description of the yuv422p format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 1, "y8", 8 },
        { 2, 1, 1, "u8", 8 },
        { 2, 1, 1, "v8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p);

/** @This is the description of the yuv444p format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 1, "y8", 8 },
        { 1, 1, 1, "u8", 8 },
        { 1, 1, 1, "v8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p);

/** @This is the description of the yuv420p10le format */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p10le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y10l", 10 },
        { 2, 2, 2, "u10l", 10 },
        { 2, 2, 2, "v10l", 10 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p10le);

/** @This is the description of the yuv422p10le format */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p10le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y10l", 10 },
        { 2, 1, 2, "u10l", 10 },
        { 2, 1, 2, "v10l", 10 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p10le);

/** @This is the description of the yuv444p10le format */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p10le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y10l", 10 },
        { 1, 1, 2, "u10l", 10 },
        { 1, 1, 2, "v10l", 10 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p10le);

/** @This is the description of the yuv420p10be format */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p10be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y10b", 10 },
        { 2, 2, 2, "u10b", 10 },
        { 2, 2, 2, "v10b", 10 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p10be);

/** @This is the description of the yuv422p10be format */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p10be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y10b", 10 },
        { 2, 1, 2, "u10b", 10 },
        { 2, 1, 2, "v10b", 10 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p10be);

/** @This is the description of the yuv444p10be format */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p10be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y10b", 10 },
        { 1, 1, 2, "u10b", 10 },
        { 1, 1, 2, "v10b", 10 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p10be);

/** @This is the description of the yuv420p12le format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p12le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y12l", 12 },
        { 2, 2, 2, "u12l", 12 },
        { 2, 2, 2, "v12l", 12 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p12le);

/** @This is the description of the yuv422p12le format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p12le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y12l", 12 },
        { 2, 1, 2, "u12l", 12 },
        { 2, 1, 2, "v12l", 12 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p12le);

/** @This is the description of the yuv444p12le format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p12le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y12l", 12 },
        { 1, 1, 2, "u12l", 12 },
        { 1, 1, 2, "v12l", 12 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p12le);

/** @This is the description of the yuv420p12be format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p12be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y12b", 12 },
        { 2, 2, 2, "u12b", 12 },
        { 2, 2, 2, "v12b", 12 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p12be);

/** @This is the description of the yuv422p12be format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p12be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y12b", 12 },
        { 2, 1, 2, "u12b", 12 },
        { 2, 1, 2, "v12b", 12 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p12be);

/** @This is the description of the yuv444p12be format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p12be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y12b", 12 },
        { 1, 1, 2, "u12b", 12 },
        { 1, 1, 2, "v12b", 12 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p12be);

/** @This is the description of the yuv420p16le format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p16le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y16l", 16 },
        { 2, 2, 2, "u16l", 16 },
        { 2, 2, 2, "v16l", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p16le);

/** @This is the description of the yuv422p16le format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p16le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y16l", 16 },
        { 2, 1, 2, "u16l", 16 },
        { 2, 1, 2, "v16l", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p16le);

/** @This is the description of the yuv444p16le format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p16le = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y16l", 16 },
        { 1, 1, 2, "u16l", 16 },
        { 1, 1, 2, "v16l", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p16le);

/** @This is the description of the yuv420p16be format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv420p16be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y16b", 16 },
        { 2, 2, 2, "u16b", 16 },
        { 2, 2, 2, "v16b", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv420p16be);

/** @This is the description of the yuv422p16be format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv422p16be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y16b", 16 },
        { 2, 1, 2, "u16b", 16 },
        { 2, 1, 2, "v16b", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv422p16be);

/** @This is the description of the yuv444p16be format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuv444p16be = {
    .macropixel = 1,
    .nb_planes = 3,
    .planes = {
        { 1, 1, 2, "y16b", 16 },
        { 1, 1, 2, "u16b", 16 },
        { 1, 1, 2, "v16b", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuv444p16be);

/** @This is the description of the yuyv422 format. */
static const struct uref_pic_flow_format uref_pic_flow_format_yuyv422 = {
    .macropixel = 2,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 4, "y8u8y8v8", 32 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(yuyv422);

/** @This is the description of the uyvy422 format. */
static const struct uref_pic_flow_format uref_pic_flow_format_uyvy422 = {
    .macropixel = 2,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 4, "u8y8v8y8", 32 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(uyvy422);

/** This is the description of the gray8 format. */
static const struct uref_pic_flow_format uref_pic_flow_format_gray8 = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 1, "y8", 8 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(gray8);

/** This is the description of the rgb565 format. */
static const struct uref_pic_flow_format uref_pic_flow_format_rgb565 = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 2, "r5g6b5", 16 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(rgb565);

/** @This is the description of the rgb24 format. */
static const struct uref_pic_flow_format uref_pic_flow_format_rgb24 = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 3, "r8g8b8", 24 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(rgb24);

/** @This is the description of the bgr format. */
static const struct uref_pic_flow_format uref_pic_flow_format_bgr24 = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 3, "b8g8e8", 24 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(bgr24);

/** @This is the description of the argb format. */
static const struct uref_pic_flow_format uref_pic_flow_format_argb = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 4, "a8r8g8b8", 32 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(argb);

/** @This is the description of the rgba format. */
static const struct uref_pic_flow_format uref_pic_flow_format_rgba = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 4, "r8g8b8a8", 32 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(rgba);

/** @This is the description of the abgr format. */
static const struct uref_pic_flow_format uref_pic_flow_format_abgr = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 4, "a8b8g8r8", 32 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(abgr);

/** @This is the description of the bgra format. */
static const struct uref_pic_flow_format uref_pic_flow_format_bgra = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 4, "b8g8r8a8", 32 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(bgra);

/** @This is the description of the rgba64be. */
static const struct uref_pic_flow_format uref_pic_flow_format_rgba64be = {
    .macropixel = 1,
    .nb_planes = 1,
    .planes = {
        { 1, 1, 8, "r16g16b16a16", 64 },
    },
};

UREF_PIC_FLOW_FORMAT_HELPER(rgba64be);

#ifdef __cplusplus
}
#endif
#endif

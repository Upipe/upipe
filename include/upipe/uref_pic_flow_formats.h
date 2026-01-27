/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
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

#include "upipe/uref_pic_flow.h"

/** @This describes a picture plane */
struct uref_pic_flow_format_plane {
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
};

/** @This describes a picture format. */
struct uref_pic_flow_format {
    /** name */
    const char *name;
    /** macro pixel */
    uint8_t macropixel;
    /** number of planes */
    uint8_t nb_planes;
    /** array of plane descriptions */
    struct uref_pic_flow_format_plane planes[];
};

/** @This iterates the planes of an uref_pic_flow_format.
 *
 * @param flow_format flow format to iterate
 * @param plane set with the next plane
 */
#define uref_pic_flow_format_foreach_plane(flow_format, plane)  \
    for (plane = flow_format->planes;                           \
         plane < flow_format->planes + flow_format->nb_planes;  \
         plane++)

/** @This returns the corresponding plane of a flow format.
 *
 * @param flow_format picture flow format to look into
 * @param chroma chroma plane to get
 * @return the flow format plane or NULL
 */
static inline const struct uref_pic_flow_format_plane *
uref_pic_flow_format_get_plane(const struct uref_pic_flow_format *flow_format,
                               const char *chroma)
{
    const struct uref_pic_flow_format_plane *plane;
    uref_pic_flow_format_foreach_plane(flow_format, plane)
        if (chroma && !strcmp(chroma, plane->chroma))
            return plane;
    return NULL;
}

/** @This returns the index of a plane in the flow format plane array.
 *
 * @param flow_format pointer to the structure containing the array
 * @param plane plane to get the index from
 * @return the index or UINT8_MAX if the plane was not found in the array
 */
static inline uint8_t
uref_pic_flow_format_get_plane_id(const struct uref_pic_flow_format *flow_format,
                                  const struct uref_pic_flow_format_plane *plane)
{
    if (plane < flow_format->planes)
        return UINT8_MAX;

    if (plane - flow_format->planes > UINT8_MAX ||
        plane - flow_format->planes > flow_format->nb_planes)
        return UINT8_MAX;
    return plane - flow_format->planes;
}

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

#define UREF_PIC_FLOW_FORMAT_NB_PLANES(...)                                 \
    (sizeof (__VA_ARGS__) / sizeof ((__VA_ARGS__)[0]))

#define UREF_PIC_FLOW_FORMAT(Name, MacroPixel, ...)                         \
static const struct uref_pic_flow_format uref_pic_flow_format_##Name = {    \
    .name = #Name,                                                          \
    .macropixel = MacroPixel,                                               \
    .nb_planes = UREF_PIC_FLOW_FORMAT_NB_PLANES(                            \
        (struct uref_pic_flow_format_plane []){ __VA_ARGS__ }),             \
    .planes = {                                                             \
        __VA_ARGS__                                                         \
    },                                                                      \
}

/** @This is the description of the yuva420p format. */
UREF_PIC_FLOW_FORMAT(yuva420p, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 2, 2, 1, "u8", 8 },
                     { 2, 2, 1, "v8", 8 },
                     { 1, 1, 1, "a8", 8 });

/** @This is the description of the yuva422p format. */
UREF_PIC_FLOW_FORMAT(yuva422p, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 2, 1, 1, "u8", 8 },
                     { 2, 1, 1, "v8", 8 },
                     { 1, 1, 1, "a8", 8 });

/** @This is the description of the yuva444p format. */
UREF_PIC_FLOW_FORMAT(yuva444p, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 1, 1, 1, "u8", 8 },
                     { 1, 1, 1, "v8", 8 },
                     { 1, 1, 1, "a8", 8 });

/** @This is the description of the yuv420p format. */
UREF_PIC_FLOW_FORMAT(yuv420p, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 2, 2, 1, "u8", 8 },
                     { 2, 2, 1, "v8", 8 });

/** @This is the description of the yuv422p format. */
UREF_PIC_FLOW_FORMAT(yuv422p, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 2, 1, 1, "u8", 8 },
                     { 2, 1, 1, "v8", 8 });

/** @This is the description of the yuv444p format. */
UREF_PIC_FLOW_FORMAT(yuv444p, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 1, 1, 1, "u8", 8 },
                     { 1, 1, 1, "v8", 8 });

/** @This is the description of the yuva420p10le format */
UREF_PIC_FLOW_FORMAT(yuva420p10le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 2, 2, 2, "u10l", 10 },
                     { 2, 2, 2, "v10l", 10 },
                     { 1, 1, 2, "a10l", 10 });

/** @This is the description of the yuva422p10le format */
UREF_PIC_FLOW_FORMAT(yuva422p10le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 2, 1, 2, "u10l", 10 },
                     { 2, 1, 2, "v10l", 10 },
                     { 1, 1, 2, "a10l", 10 });

/** @This is the description of the yuva444p10le format */
UREF_PIC_FLOW_FORMAT(yuva444p10le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 1, 1, 2, "u10l", 10 },
                     { 1, 1, 2, "v10l", 10 },
                     { 1, 1, 2, "a10l", 10 });

/** @This is the description of the yuv420p10le format */
UREF_PIC_FLOW_FORMAT(yuv420p10le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 2, 2, 2, "u10l", 10 },
                     { 2, 2, 2, "v10l", 10 });

/** @This is the description of the yuv422p10le format */
UREF_PIC_FLOW_FORMAT(yuv422p10le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 2, 1, 2, "u10l", 10 },
                     { 2, 1, 2, "v10l", 10 });

/** @This is the description of the yuv444p10le format */
UREF_PIC_FLOW_FORMAT(yuv444p10le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 1, 1, 2, "u10l", 10 },
                     { 1, 1, 2, "v10l", 10 });

/** @This is the description of the yuv420p10be format */
UREF_PIC_FLOW_FORMAT(yuv420p10be, 1,
                     { 1, 1, 2, "y10b", 10 },
                     { 2, 2, 2, "u10b", 10 },
                     { 2, 2, 2, "v10b", 10 });

/** @This is the description of the yuv422p10be format */
UREF_PIC_FLOW_FORMAT(yuv422p10be, 1,
                     { 1, 1, 2, "y10b", 10 },
                     { 2, 1, 2, "u10b", 10 },
                     { 2, 1, 2, "v10b", 10 });

/** @This is the description of the yuv444p10be format */
UREF_PIC_FLOW_FORMAT(yuv444p10be, 1,
                     { 1, 1, 2, "y10b", 10 },
                     { 1, 1, 2, "u10b", 10 },
                     { 1, 1, 2, "v10b", 10 });

/** @This is the description of the yuv420p12le format. */
UREF_PIC_FLOW_FORMAT(yuv420p12le, 1,
                     { 1, 1, 2, "y12l", 12 },
                     { 2, 2, 2, "u12l", 12 },
                     { 2, 2, 2, "v12l", 12 });

/** @This is the description of the yuv422p12le format. */
UREF_PIC_FLOW_FORMAT(yuv422p12le, 1,
                     { 1, 1, 2, "y12l", 12 },
                     { 2, 1, 2, "u12l", 12 },
                     { 2, 1, 2, "v12l", 12 });

/** @This is the description of the yuv444p12le format. */
UREF_PIC_FLOW_FORMAT(yuv444p12le, 1,
                     { 1, 1, 2, "y12l", 12 },
                     { 1, 1, 2, "u12l", 12 },
                     { 1, 1, 2, "v12l", 12 });

/** @This is the description of the yuv420p12be format. */
UREF_PIC_FLOW_FORMAT(yuv420p12be, 1,
                     { 1, 1, 2, "y12b", 12 },
                     { 2, 2, 2, "u12b", 12 },
                     { 2, 2, 2, "v12b", 12 });

/** @This is the description of the yuv422p12be format. */
UREF_PIC_FLOW_FORMAT(yuv422p12be, 1,
                     { 1, 1, 2, "y12b", 12 },
                     { 2, 1, 2, "u12b", 12 },
                     { 2, 1, 2, "v12b", 12 });

/** @This is the description of the yuv444p12be format. */
UREF_PIC_FLOW_FORMAT(yuv444p12be, 1,
                     { 1, 1, 2, "y12b", 12 },
                     { 1, 1, 2, "u12b", 12 },
                     { 1, 1, 2, "v12b", 12 });

/** @This is the description of the yuv420p16le format. */
UREF_PIC_FLOW_FORMAT(yuv420p16le, 1,
                     { 1, 1, 2, "y16l", 16 },
                     { 2, 2, 2, "u16l", 16 },
                     { 2, 2, 2, "v16l", 16 });

/** @This is the description of the yuv422p16le format. */
UREF_PIC_FLOW_FORMAT(yuv422p16le, 1,
                     { 1, 1, 2, "y16l", 16 },
                     { 2, 1, 2, "u16l", 16 },
                     { 2, 1, 2, "v16l", 16 });

/** @This is the description of the yuv444p16le format. */
UREF_PIC_FLOW_FORMAT(yuv444p16le, 1,
                     { 1, 1, 2, "y16l", 16 },
                     { 1, 1, 2, "u16l", 16 },
                     { 1, 1, 2, "v16l", 16 });

/** @This is the description of the yuv420p16be format. */
UREF_PIC_FLOW_FORMAT(yuv420p16be, 1,
                     { 1, 1, 2, "y16b", 16 },
                     { 2, 2, 2, "u16b", 16 },
                     { 2, 2, 2, "v16b", 16 });

/** @This is the description of the yuv422p16be format. */
UREF_PIC_FLOW_FORMAT(yuv422p16be, 1,
                     { 1, 1, 2, "y16b", 16 },
                     { 2, 1, 2, "u16b", 16 },
                     { 2, 1, 2, "v16b", 16 });

/** @This is the description of the yuv444p16be format. */
UREF_PIC_FLOW_FORMAT(yuv444p16be, 1,
                     { 1, 1, 2, "y16b", 16 },
                     { 1, 1, 2, "u16b", 16 },
                     { 1, 1, 2, "v16b", 16 });

/** @This is the description of the yuyv422 format. */
UREF_PIC_FLOW_FORMAT(yuyv422, 2, { 1, 1, 4, "y8u8y8v8", 32 });

/** @This is the description of the uyvy422 format. */
UREF_PIC_FLOW_FORMAT(uyvy422, 2, { 1, 1, 4, "u8y8v8y8", 32 });

/** This is the description of the gray8 format. */
UREF_PIC_FLOW_FORMAT(gray8, 1, { 1, 1, 1, "y8", 8 });

/** This is the description of the mono black format.
 *
 * From libavutil/pixfmt.h:
 * 1bpp, 0 is black, 1 is white, in each byte pixels are ordered from the msb
 * to the lsb
 */
UREF_PIC_FLOW_FORMAT(monoblack, 1, { 1, 1, 1, "y1", 1 });

/** This is the description of the mono white format.
 *
 * From libavutil/pixfmt.h:
 * 1bpp, 0 is white, 1 is black, in each byte pixels are ordered from the msb
 * to the lsb
 */
UREF_PIC_FLOW_FORMAT(monowhite, 1, { 1, 1, 1, "Y1", 1 });

/** @This is the description of the rgb0 format.
 *
 * From libavutil/pixfmt.h:
 * packed RGB 8:8:8, 32bpp, RGBXRGBX...  X=unused/undefined
 */
UREF_PIC_FLOW_FORMAT(rgb0, 1, { 1, 1, 1, "r8g8b808", 32 });

/** @This is the description of the 0rgb format.
 *
 * from libavutil/pixfmt.h:
 * packed RGB 8:8:8, 32bpp, XRGBXRGB... X=unused/undefined
 */
UREF_PIC_FLOW_FORMAT(0rgb, 1, { 1, 1, 1, "08r8g8b8", 32 });

/** This is the description of the rgb565 format. */
UREF_PIC_FLOW_FORMAT(rgb565, 1, { 1, 1, 2, "r5g6b5", 16 });

/** @This is the description of the rgb24 format. */
UREF_PIC_FLOW_FORMAT(rgb24, 1, { 1, 1, 3, "r8g8b8", 24 });

/** @This is the description of the bgr format. */
UREF_PIC_FLOW_FORMAT(bgr24, 1, { 1, 1, 3, "b8g8e8", 24 });

/** @This is the description of the argb format. */
UREF_PIC_FLOW_FORMAT(argb, 1, { 1, 1, 4, "a8r8g8b8", 32 });

/** @This is the description of the rgba format. */
UREF_PIC_FLOW_FORMAT(rgba, 1, { 1, 1, 4, "r8g8b8a8", 32 });

/** @This is the description of the abgr format. */
UREF_PIC_FLOW_FORMAT(abgr, 1, { 1, 1, 4, "a8b8g8r8", 32 });

/** @This is the description of the bgra format. */
UREF_PIC_FLOW_FORMAT(bgra, 1, { 1, 1, 4, "b8g8r8a8", 32 });

/** @This is the description of the rgba64le format. */
UREF_PIC_FLOW_FORMAT(rgba64le, 1, { 1, 1, 8, "r16g16b16a16l", 64 });

/** @This is the description of the rgba64be format. */
UREF_PIC_FLOW_FORMAT(rgba64be, 1, { 1, 1, 8, "r16g16b16a16", 64 });

/** @This is the description of the nv12 format. */
UREF_PIC_FLOW_FORMAT(nv12, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 2, 2, 2, "u8v8", 16 });

/** @This is the description of the nv16 format. */
UREF_PIC_FLOW_FORMAT(nv16, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 2, 1, 2, "u8v8", 16 });

/** @This is the description of the nv24 format. */
UREF_PIC_FLOW_FORMAT(nv24, 1,
                     { 1, 1, 1, "y8", 8 },
                     { 1, 1, 2, "u8v8", 16 });

/** @This is the description of the gbrp format. */
UREF_PIC_FLOW_FORMAT(gbrp, 1,
                     { 1, 1, 1, "g8", 8 },
                     { 1, 1, 1, "b8", 8 },
                     { 1, 1, 1, "r8", 8 });

/** @This is the description of the p010le format. */
UREF_PIC_FLOW_FORMAT(p010le, 1,
                     { 1, 1, 2, "y10l", 10 },
                     { 2, 2, 4, "u10v10l", 20 });

#define UREF_PIC_FLOW_FORMAT_FOREACH(Do, ...) \
    Do(yuva420p, ## __VA_ARGS__) \
    Do(yuva422p, ## __VA_ARGS__) \
    Do(yuva444p, ## __VA_ARGS__) \
    Do(yuv420p, ## __VA_ARGS__) \
    Do(yuv422p, ## __VA_ARGS__) \
    Do(yuv444p, ## __VA_ARGS__) \
    Do(yuva420p10le, ## __VA_ARGS__) \
    Do(yuva422p10le, ## __VA_ARGS__) \
    Do(yuva444p10le, ## __VA_ARGS__) \
    Do(yuv420p10le, ## __VA_ARGS__) \
    Do(yuv422p10le, ## __VA_ARGS__) \
    Do(yuv444p10le, ## __VA_ARGS__) \
    Do(yuv420p10be, ## __VA_ARGS__) \
    Do(yuv422p10be, ## __VA_ARGS__) \
    Do(yuv444p10be, ## __VA_ARGS__) \
    Do(yuv420p12le, ## __VA_ARGS__) \
    Do(yuv422p12le, ## __VA_ARGS__) \
    Do(yuv444p12le, ## __VA_ARGS__) \
    Do(yuv420p12be, ## __VA_ARGS__) \
    Do(yuv422p12be, ## __VA_ARGS__) \
    Do(yuv444p12be, ## __VA_ARGS__) \
    Do(yuv420p16le, ## __VA_ARGS__) \
    Do(yuv422p16le, ## __VA_ARGS__) \
    Do(yuv444p16le, ## __VA_ARGS__) \
    Do(yuv420p16be, ## __VA_ARGS__) \
    Do(yuv422p16be, ## __VA_ARGS__) \
    Do(yuv444p16be, ## __VA_ARGS__) \
    Do(yuyv422, ## __VA_ARGS__) \
    Do(uyvy422, ## __VA_ARGS__) \
    Do(gray8, ## __VA_ARGS__) \
    Do(monoblack, ## __VA_ARGS__) \
    Do(monowhite, ## __VA_ARGS__) \
    Do(rgb0, ## __VA_ARGS__) \
    Do(0rgb, ## __VA_ARGS__) \
    Do(rgb565, ## __VA_ARGS__) \
    Do(rgb24, ## __VA_ARGS__) \
    Do(bgr24, ## __VA_ARGS__) \
    Do(argb, ## __VA_ARGS__) \
    Do(rgba, ## __VA_ARGS__) \
    Do(abgr, ## __VA_ARGS__) \
    Do(bgra, ## __VA_ARGS__) \
    Do(rgba64le, ## __VA_ARGS__) \
    Do(rgba64be, ## __VA_ARGS__) \
    Do(nv12, ## __VA_ARGS__) \
    Do(nv16, ## __VA_ARGS__) \
    Do(nv24, ## __VA_ARGS__) \
    Do(gbrp, ## __VA_ARGS__) \
    Do(p010le, ## __VA_ARGS__) \

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

#define Do(Type)    UREF_PIC_FLOW_FORMAT_HELPER(Type)
UREF_PIC_FLOW_FORMAT_FOREACH(Do)
#undef Do

static const struct uref_pic_flow_format *uref_pic_flow_formats[] = {
#define Do(Type)    &uref_pic_flow_format_##Type,
    UREF_PIC_FLOW_FORMAT_FOREACH(Do)
#undef Do
};

static inline const struct uref_pic_flow_format *
uref_pic_flow_get_format(struct uref *uref)
{
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(uref_pic_flow_formats); i++) {
        const struct uref_pic_flow_format *format = uref_pic_flow_formats[i];
        if (ubase_check(uref_pic_flow_check_format(uref, format)))
            return format;
    }
    return NULL;
}

/** @This finds a picture format with the given name.
 *
 * @param name name of the picture format to lookup
 * @return the corresponding picture format, or NULL if not found
 */
static inline const struct uref_pic_flow_format *
uref_pic_flow_get_format_by_name(const char *name)
{
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(uref_pic_flow_formats); i++)
        if (!strcmp(uref_pic_flow_formats[i]->name, name))
            return uref_pic_flow_formats[i];

    return NULL;
}

#ifdef __cplusplus
}
#endif
#endif

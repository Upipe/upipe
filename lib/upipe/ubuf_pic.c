/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe buffer handling for picture managers
 * This file defines the picture-specific API to access buffers.
 */

#include "upipe/ubuf_pic.h"

#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) && defined(__x86_64__)
#include "ubuf_pic_blit_x86.h"
#endif

/** @This blits a picture ubuf to another ubuf.
 *
 * @param dest destination ubuf
 * @param src source ubuf
 * @param dest_hoffset number of pixels to seek at the beginning of each line of
 * dest
 * @param dest_voffset number of lines to seek at the beginning of dest
 * @param src_hoffset number of pixels to skip at the beginning of each line of
 * src
 * @param src_voffset number of lines to skip at the beginning of src
 * @param extract_hsize horizontal size to copy
 * @param extract_vsize vertical size to copy
 * @param alpha_plane pointer to alpha plane buffer, if any
 * @param alpha_stride horizontal stride of the alpha plane buffer
 * @param alpha alpha multiplier
 * @param threshold alpha blending method
 *    0 means ignore alpha
 *    255 means blends src and dest together using alpha levels (slow)
 *    Any value in between means using the src pixels if and only if
 *      their alpha value is more than this value
 * @return an error code
 */
int ubuf_pic_blit_alpha(struct ubuf *dest, struct ubuf *src,
                                int dest_hoffset, int dest_voffset,
                                int src_hoffset, int src_voffset,
                                int extract_hsize, int extract_vsize,
                                const uint8_t *alpha_plane, int alpha_stride,
                                const int alpha, const int threshold)
{
    if (alpha_plane == NULL && alpha < threshold && threshold != 0xff)
        return UBASE_ERR_NONE; /* nothing to do */

    uint8_t src_macropixel;
    UBASE_RETURN(ubuf_pic_size(src, NULL, NULL, &src_macropixel))
    uint8_t dest_macropixel;
    UBASE_RETURN(ubuf_pic_size(dest, NULL, NULL, &dest_macropixel))
    if (unlikely(dest_macropixel != src_macropixel))
        return UBASE_ERR_INVALID;

#define MAX_PLANES  3

    const char *chroma;
    ubuf_pic_foreach_plane(dest, chroma) {
        const char *in_chroma[MAX_PLANES] = { chroma };
        unsigned in_planes = 1;
        int err;

        size_t dest_stride;
        uint8_t dest_hsub, dest_vsub, dest_macropixel_size;
        UBASE_RETURN(ubuf_pic_plane_size(dest, chroma,
                     &dest_stride, &dest_hsub, &dest_vsub,
                     &dest_macropixel_size))

        size_t src_stride;
        uint8_t src_hsub, src_vsub, src_macropixel_size;
        err = ubuf_pic_plane_size(src, in_chroma[0], &src_stride, &src_hsub,
                                  &src_vsub, &src_macropixel_size);
        if (!ubase_check(err)) {
            if (!strcmp(chroma, "u8v8")) {
                in_chroma[0] = "u8";
                in_chroma[1] = "v8";
                in_planes = 2;
            } else
                return err;

            for (unsigned i = 0; i < in_planes; i++) {
                UBASE_RETURN(ubuf_pic_plane_size(src, in_chroma[i], &src_stride,
                                                 &src_hsub, &src_vsub,
                                                 &src_macropixel_size));
                if (unlikely(src_hsub != dest_hsub || src_vsub != dest_vsub ||
                             src_macropixel_size !=
                                 dest_macropixel_size / in_planes))
                    return UBASE_ERR_INVALID;
            }
        } else if (unlikely(src_hsub != dest_hsub || src_vsub != dest_vsub ||
                            src_macropixel_size != dest_macropixel_size))
            return UBASE_ERR_INVALID;

        uint8_t *dest_buffer;
        const uint8_t *in[MAX_PLANES];
        UBASE_RETURN(ubuf_pic_plane_write(dest, chroma,
                    dest_hoffset, dest_voffset,
                    extract_hsize, extract_vsize, &dest_buffer))
        for (unsigned i = 0; i < in_planes; i++) {
            err =
                ubuf_pic_plane_read(src, in_chroma[i], src_hoffset, src_voffset,
                                    extract_hsize, extract_vsize, &in[i]);
            if (unlikely(!ubase_check(err))) {
                for (; i; i--)
                    ubuf_pic_plane_unmap(src, in_chroma[i - 1], src_hoffset,
                                         src_voffset, extract_hsize,
                                         extract_vsize);
                ubuf_pic_plane_unmap(dest, chroma, dest_hoffset, dest_voffset,
                                     extract_hsize, extract_vsize);
                return err;
            }
        }

        int plane_hsize = extract_hsize / src_hsub / src_macropixel *
                          src_macropixel_size;
        int plane_vsize = extract_vsize / src_vsub;

        for (int i = 0; i < plane_vsize; i++) {
            if ((!alpha_plane && alpha == 0xff) || threshold == 0) {
                if (in_planes == 1)
                    memcpy(dest_buffer, in[0], plane_hsize);
                else {
                    for (int j = 0; j < plane_hsize; j++) {
                        for (int p = 0; p < in_planes; p++)
                            dest_buffer[j * in_planes + p] =
                                (dest_buffer[j * in_planes + p] *
                                     (0xff - alpha) +
                                 in[p][j] * alpha) /
                                0xff;
                    }
                }
            } else if (!alpha_plane) {
                for (int j = 0; j < plane_hsize; j++) {
                    for (int p = 0; p < in_planes; p++)
                        dest_buffer[j * in_planes + p] =
                            (dest_buffer[j * in_planes + p] * (0xff - alpha) +
                             in[p][j] * alpha) /
                            0xff;
                }
            } else if (threshold != 0xff) {
                /* This is an on/off blending
                 * if alpha is over the threshold, we use the subpicture pixel.
                 */
                if (alpha == 0xff) {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub];
                        if (a > threshold) {
                            for (int p = 0; p < in_planes; p++)
                                dest_buffer[j * in_planes + p] = in[p][j];
                        }
                    }
                } else {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = (uint16_t)alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub] * (uint16_t)alpha / 0xff;
                        if (a > threshold) {
                            for (int p = 0; p < in_planes; p++)
                                dest_buffer[j * in_planes + p] = in[p][j];
                        }
                    }
                }
            } else {
                /* smooth and slow blending */
                if (alpha == 0xff) {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub];
                        for (int p = 0; p < in_planes; p++)
                            dest_buffer[j * in_planes + p] =
                                (dest_buffer[j * in_planes + p] * (0xff - a) +
                                 in[p][j] * a) /
                                0xff;
                    }
                } else {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = (uint16_t)alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub] * (uint16_t)alpha / 0xff;
                        for (int p = 0; p < in_planes; p++)
                            dest_buffer[j * in_planes + p] =
                                (dest_buffer[j * in_planes + p] * (0xff - a) +
                                 in[p][j] * a) /
                                0xff;
                    }
                }
            }
            dest_buffer += dest_stride;
            for (int p = 0; p < in_planes; p++)
                in[p] += src_stride;
        }

        err = ubuf_pic_plane_unmap(dest, chroma,
                                   dest_hoffset, dest_voffset,
                                   extract_hsize, extract_vsize);
        for (int p = 0; p < in_planes; p++)
            UBASE_RETURN(ubuf_pic_plane_unmap(src, in_chroma[p], src_hoffset,
                                              src_voffset, extract_hsize,
                                              extract_vsize))
        UBASE_RETURN(err)
    }

#undef MAX_PLANES

    return UBASE_ERR_NONE;
}

/** @see ubuf_pic_blit_alpha */
int ubuf_pic_blit_alpha10(struct ubuf *dest, struct ubuf *src,
                                int dest_hoffset, int dest_voffset,
                                int src_hoffset, int src_voffset,
                                int extract_hsize, int extract_vsize,
                                const uint8_t *alpha_plane, int alpha_stride,
                                const int alpha, const int threshold)
{
    if (alpha_plane == NULL && alpha < threshold && threshold != 0x3ff)
        return UBASE_ERR_NONE; /* nothing to do */

    uint8_t src_macropixel;
    UBASE_RETURN(ubuf_pic_size(src, NULL, NULL, &src_macropixel))
    uint8_t dest_macropixel;
    UBASE_RETURN(ubuf_pic_size(dest, NULL, NULL, &dest_macropixel))
    if (unlikely(dest_macropixel != src_macropixel))
        return UBASE_ERR_INVALID;

    const char *chroma;
    ubuf_pic_foreach_plane(dest, chroma) {
        size_t src_stride;
        uint8_t src_hsub, src_vsub, src_macropixel_size;
        UBASE_RETURN(ubuf_pic_plane_size(src, chroma, &src_stride,
                    &src_hsub, &src_vsub, &src_macropixel_size))

        size_t dest_stride;
        uint8_t dest_hsub, dest_vsub, dest_macropixel_size;
        UBASE_RETURN(ubuf_pic_plane_size(dest, chroma,
                     &dest_stride, &dest_hsub, &dest_vsub,
                     &dest_macropixel_size))

        if (unlikely(src_hsub != dest_hsub || src_vsub != dest_vsub ||
                     src_macropixel_size != dest_macropixel_size))
            return UBASE_ERR_INVALID;

        uint8_t *dest_buffer;
        const uint8_t *src_buffer;
        UBASE_RETURN(ubuf_pic_plane_write(dest, chroma,
                    dest_hoffset, dest_voffset,
                    extract_hsize, extract_vsize, &dest_buffer))
        int err = ubuf_pic_plane_read(src, chroma, src_hoffset, src_voffset,
                                      extract_hsize, extract_vsize,
                                      &src_buffer);
        if (unlikely(!ubase_check(err))) {
            ubuf_pic_plane_unmap(dest, chroma,
                                 dest_hoffset, dest_voffset,
                                 extract_hsize, extract_vsize);
            return err;
        }

        size_t plane_hsize = extract_hsize / src_hsub / src_macropixel *
                          src_macropixel_size / 2;
        size_t plane_vsize = extract_vsize / src_vsub;

        for (size_t i = 0; i < plane_vsize; i++) {
            uint16_t *real_dst = (uint16_t *)dest_buffer;
            const uint16_t *real_src = (const uint16_t *)src_buffer;
            const uint16_t *real_alpha = (const uint16_t *)(alpha_plane + alpha_stride * i * src_vsub);

#if defined(__GNUC__) && defined(__x86_64__)
            if (blit10_handle_cases(real_dst, real_src, real_alpha, alpha,
                        threshold, plane_hsize, src_hsub))
                /* Nothing to do */;
            else
#endif

            if ((!alpha_plane && alpha == 0x3ff) || threshold == 0) {
                memcpy(dest_buffer, src_buffer, plane_hsize*2);
            } else if (!alpha_plane) {
                for (size_t j = 0; j < plane_hsize; j++) {
                    real_dst[j] = (real_dst[j] * (0x3ff - alpha) + real_src[j] * alpha) / 0x3ff;
                }
            } else if (threshold != 0x3ff) {
                /* This is an on/off blending
                 * if alpha is over the threshold, we use the subpicture pixel.
                 */
                if (alpha == 0x3ff) {
                    for (size_t j = 0; j < plane_hsize; j++) {
                        const uint16_t a = real_alpha[j * src_hsub];
                        if (a > threshold) real_dst[j] = real_src[j];
                    }
                } else {
                    for (size_t j = 0; j < plane_hsize; j++) {
                        const uint16_t a = real_alpha[j * src_hsub] * alpha / 0x3ff;
                        if (a > threshold) real_dst[j] = real_src[j];
                    }
                }
            } else {
                /* smooth and slow blending */
                if (alpha == 0x3ff) {
                    for (size_t j = 0; j < plane_hsize; j++) {
                        const uint16_t a = real_alpha[j * src_hsub];
                        real_dst[j] = (real_dst[j] * (0x3ff - a) + real_src[j] * a) / 0x3ff;
                    }
                } else {
                    for (size_t j = 0; j < plane_hsize; j++) {
                        const uint16_t a = real_alpha[j * src_hsub] * alpha / 0x3ff;
                        real_dst[j] = (real_dst[j] * (0x3ff - a) + real_src[j] * a) / 0x3ff;
                    }
                }
            }
            dest_buffer += dest_stride;
            src_buffer += src_stride;
        }

        err = ubuf_pic_plane_unmap(dest, chroma,
                                   dest_hoffset, dest_voffset,
                                   extract_hsize, extract_vsize);
        UBASE_RETURN(ubuf_pic_plane_unmap(src, chroma,
                                          src_hoffset, src_voffset,
                                          extract_hsize, extract_vsize))
        UBASE_RETURN(err)
    }
    return UBASE_ERR_NONE;
}

/** @This blits a picture ubuf to another ubuf.
 *
 * @param dest destination ubuf
 * @param src source ubuf
 * @param dest_hoffset number of pixels to seek at the beginning of each line of
 * dest
 * @param dest_voffset number of lines to seek at the beginning of dest
 * @param src_hoffset number of pixels to skip at the beginning of each line of
 * src
 * @param src_voffset number of lines to skip at the beginning of src
 * @param extract_hsize horizontal size to copy
 * @param extract_vsize vertical size to copy
 * @param alpha alpha multiplier
 * @param threshold threshold parameter for alpha
 * @return an error code
 */
int ubuf_pic_blit(struct ubuf *dest, struct ubuf *src,
                                int dest_hoffset, int dest_voffset,
                                int src_hoffset, int src_voffset,
                                int extract_hsize, int extract_vsize,
                                const int alpha, const int threshold)
{
    const uint8_t *alpha_plane = NULL;
    size_t alpha_stride = 0;
    int ret;

    static const char *alpha_plane_names[] = { "a8", "a10l" };
    const char *apn;
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(alpha_plane_names); i++) {
        apn = alpha_plane_names[i];

        /* Check for the existence of the given alpha plane. */
        ret = ubuf_pic_plane_read(src, apn, 0, 0, -1, -1, &alpha_plane);
        /* Continue to the next if it isn't found. */
        if (!ubase_check(ret))
            continue;

        /* Get the stride. */
        ret = ubuf_pic_plane_size(src, apn, &alpha_stride, NULL, NULL, NULL);
        /* Jump out of the loop if there is an error. */
        if (!ubase_check(ret))
            goto end;

        /* Perform blit. */
        if (i == 0)
            ret = ubuf_pic_blit_alpha(dest, src, dest_hoffset, dest_voffset,
                    src_hoffset, src_voffset,
                    extract_hsize, extract_vsize,
                    alpha_plane, alpha_stride, alpha, threshold);
        else
            ret = ubuf_pic_blit_alpha10(dest, src, dest_hoffset, dest_voffset,
                    src_hoffset, src_voffset,
                    extract_hsize, extract_vsize,
                    alpha_plane, alpha_stride, alpha, threshold);

        /* Jump out of the loop when it is done. */
        goto end;
    }

    /* Fallback to no alpha plane. */
    ret = ubuf_pic_blit_alpha(dest, src, dest_hoffset, dest_voffset,
            src_hoffset, src_voffset,
            extract_hsize, extract_vsize,
            NULL, 0, alpha, threshold);

end:
    if (alpha_plane)
        ubuf_pic_plane_unmap(src, apn, 0, 0, -1, -1);

    return ret;
}

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
#define MATCH(a) (strcmp(chroma, a) == 0)
#define SET_COLOR(...) \
        uint8_t pattern[] = { __VA_ARGS__ }; \
        return ubuf_pic_plane_set_color(ubuf, chroma, \
                                        hoffset, voffset, hsize, vsize, \
                                        pattern, sizeof pattern)

    if (MATCH("a8")) {
        /* Assume alpha to be always full range */
        SET_COLOR(0);
    } else if (MATCH("r8g8b8a8") || MATCH("b8g8r8a8")) {
        if (fullrange) {
            SET_COLOR(0);
        } else {
            SET_COLOR(16, 16, 16, 0);
        }
    } else if (MATCH("a8r8g8b8") || MATCH("a8b8g8r8")) {
        if (fullrange) {
            SET_COLOR(0);
        } else {
            SET_COLOR(0, 16, 16, 16);
        }
    } else if (MATCH("y8") || MATCH("r8g8b8") || MATCH("b8g8r8")) {
        SET_COLOR(fullrange ? 0 : 16);

    } else if (MATCH("y16l")) {
        if (fullrange) {
            SET_COLOR(0);
        } else {
            SET_COLOR(0, 16);
        }
    } else if (MATCH("y16b")) {
        if (fullrange) {
            SET_COLOR(0);
        } else {
            SET_COLOR(16, 0);
        }
    } else if (MATCH("u8") || MATCH("v8") || MATCH("u8v8")) {
        SET_COLOR(0x80);

    } else if (MATCH("y10l")) {
        SET_COLOR(fullrange ? 0 : 0x40, 0x00);

    } else if (MATCH("u10l") || MATCH("v10l")) {
        SET_COLOR(0x00, 0x02);

    } else if (MATCH("u16l") || MATCH("v16l")) {
        SET_COLOR(0x00, 0x80);

    } else if (MATCH("u16b") || MATCH("v16b")) {
        SET_COLOR(0x80, 0x00);

    } else if (MATCH("u8y8v8y8")) {
        SET_COLOR(0x80, fullrange ? 0 : 16, 0x80, fullrange ? 0 : 16);

    } else if (MATCH("y8u8y8v8")) {
        SET_COLOR(fullrange ? 0 : 16, 0x80, fullrange ? 0 : 16, 0x80);

    } else if (MATCH("u10y10v10y10u10y10v10y10u10y10v10y10")) {
        if (fullrange) {
            SET_COLOR(0x00, 0x02, 0x00, 0x20, 0x00, 0x00, 0x08, 0x00);
        } else {
            SET_COLOR(0x00, 0x42, 0x00, 0x20, 0x10, 0x00, 0x08, 0x01);
        }
    }

#undef MATCH
#undef SET_COLOR

    return UBASE_ERR_INVALID;
}

/** @This sets (part of) the color of the specified plane.
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
 * @param pattern color pattern to set
 * @param pattern_size size of the color pattern in bytes
 * @return an error code
 */
int ubuf_pic_plane_set_color(struct ubuf *ubuf, const char *chroma,
                             int hoffset, int voffset, int hsize, int vsize,
                             const uint8_t *pattern, size_t pattern_size)
{
    size_t stride, width, height;
    uint8_t hsub, vsub, macropixel_size, macropixel;
    uint8_t *buf = NULL;

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
    height /= vsub;

    const size_t mem_width = width * macropixel_size / hsub / macropixel;

    if (pattern_size == 1) {
        for (size_t i = 0; i < height; i++) {
            memset(buf, pattern[0], mem_width);
            buf += stride;
        }

#if defined(__GNUC__) && defined(__x86_64__)

    } else if (pattern_size <= 16 && 16 % pattern_size == 0 && mem_width % 16 == 0) {
        uint8_t __attribute__ ((aligned (16))) temp[16];
        for (size_t i = 0; i < 16; i += 1)
            temp[i] = pattern[i % pattern_size];
        register const __m128i xmm = _mm_load_si128((void*)temp);
        for (int y = 0; y < height; y++) {
            uint8_t * const t = buf;
            for (size_t x = 0; x < mem_width; x += 16) {
                _mm_storeu_si128((void*)(t + x), xmm);
            }
            buf += stride;
        }

#endif

    } else {
        for (size_t i = 0; i < mem_width; i += pattern_size)
            memcpy(buf + i, pattern, pattern_size);

        for (int i = 1; i < height; i++) {
            memcpy(buf + stride, buf, mem_width);
            buf += stride;
        }
    }

    UBASE_RETURN(ubuf_pic_plane_unmap(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize))
    return UBASE_ERR_NONE;
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

/** @This converts 8 bits RGB color to 8 bits YUV.
 *
 * @param rgb RGB color to convert
 * @param fullrange use full range if not 0
 * @param yuv filled with the converted YUV color
 */
void ubuf_pic_rgb_to_yuv(const uint8_t rgb[3], int fullrange, uint8_t yuv[3])
{
    int mat[3 * 3] = {
         66, 129,  25,
        -38, -74, 112,
        112, -94, -18,
    };
    int fullrange_mat[3 * 3] = {
         77,  150,  29,
        -43,  -84, 127,
        127, -106, -21,
    };
    int *m = fullrange ? fullrange_mat : mat;
    int yuv_i[3] = { 0, 0, 0 };
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 3; j++)
            yuv_i[i] += m[i * 3 + j] * rgb[j];
    for (unsigned i = 0; i < 3; i++)
        yuv[i] = ((yuv_i[i] + 128) >> 8) + (i ? 128 : 16);
}

/** @This parses a 8 bits RGB value.
 *
 * @param value value to parse
 * @param rgb filled with the parsed value
 * @return an error code
 */
int ubuf_pic_parse_rgb(const char *value, uint8_t rgb[3])
{
    memset(rgb, 0, 3);

    if (!value)
        return UBASE_ERR_INVALID;

    int ret = sscanf(value, "rgb(%hhu, %hhu, %hhu)",
                     &rgb[0], &rgb[1], &rgb[2]);
    if (ret != 3)
        return UBASE_ERR_INVALID;
    return UBASE_ERR_NONE;
}

/** @This parses a 8 bits RGBA value.
 *
 * @param value value to parse
 * @param rgba filled with the parsed value
 * @return an error code
 */
int ubuf_pic_parse_rgba(const char *value, uint8_t rgba[4])
{
    memset(rgba, 0, 4);

    if (!value)
        return UBASE_ERR_INVALID;

    if (ubase_check(ubuf_pic_parse_rgb(value, rgba))) {
        rgba[3] = 0xff;
        return UBASE_ERR_NONE;
    }

    float alpha;
    int ret = sscanf(value, "rgba(%hhu, %hhu, %hhu, %f)",
                     &rgba[0], &rgba[1], &rgba[2], &alpha);
    if (ret != 4)
        return UBASE_ERR_INVALID;
    rgba[3] = 0xff * alpha;
    return UBASE_ERR_NONE;
}

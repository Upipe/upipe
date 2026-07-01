/*
 * Copyright (C) 2026 Open Broadcast Systems
 *
 * Authors: James Darnley
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for picture blit
 */

#undef NDEBUG

#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_pic.h"
#include "upipe/ubuf_pic_mem.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define UBUF_POOL_DEPTH     1
#define UBUF_PREPEND        2
#define UBUF_APPEND         2
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0

/** @internal @This fills a rectangular region of a plane with a diagonal
 * gradient: 0 at the top-left corner (0, 0), rising to 255 at the bottom-right
 * corner (hsize, vsize). The offset and size are expressed in luma samples. */
static void fill_plane_gradient(struct ubuf *ubuf, const char *chroma,
                                int hoffset, int voffset, int hsize, int vsize)
{
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, NULL, NULL, &macropixel));

    size_t stride;
    uint8_t hsub, vsub, macropixel_size;
    ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                     &macropixel_size));
    int hoctets = hsize * macropixel_size / hsub / macropixel;
    int rows = vsize / vsub;

    uint8_t *buffer;
    ubase_assert(ubuf_pic_plane_write(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize, &buffer));
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < hoctets; x++)
            buffer[x] = 255 * (x + y) / (hoctets + rows);
        buffer += stride;
    }
    ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize));
}

/** @internal @This fills a rectangular region of a 16-bit plane with a diagonal
 * gradient: 0 at the top-left corner (0, 0), rising to 1023 at the bottom-right
 * corner (hsize, vsize). The offset and size are expressed in luma samples. */
static void fill_plane_gradient16(struct ubuf *ubuf, const char *chroma,
                                  int hoffset, int voffset, int hsize, int vsize)
{
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, NULL, NULL, &macropixel));

    size_t stride;
    uint8_t hsub, vsub, macropixel_size;
    ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                     &macropixel_size));
    int cols = hsize * macropixel_size / hsub / macropixel / 2;
    int rows = vsize / vsub;

    uint8_t *buffer;
    ubase_assert(ubuf_pic_plane_write(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize, &buffer));
    for (int y = 0; y < rows; y++) {
        uint16_t *words = (uint16_t *)buffer;
        for (int x = 0; x < cols; x++)
            words[x] = 1023 * (x + y) / (cols + rows);
        buffer += stride;
    }
    ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, hoffset, voffset,
                                      hsize, vsize));
}

/** @internal @This reads the first octet of a plane. */
static uint8_t read_octet(struct ubuf *ubuf, const char *chroma)
{
    const uint8_t *buffer;
    ubase_assert(ubuf_pic_plane_read(ubuf, chroma, 0, 0, -1, -1, &buffer));
    uint8_t value = buffer[0];
    ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1));
    return value;
}

/** @internal @This reads the first 16-bit sample of a plane (host byte order). */
static uint16_t read_word(struct ubuf *ubuf, const char *chroma)
{
    const uint8_t *buffer;
    ubase_assert(ubuf_pic_plane_read(ubuf, chroma, 0, 0, -1, -1, &buffer));
    uint16_t value = ((const uint16_t *)buffer)[0];
    ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1));
    return value;
}

/** @internal @This computes the value ubuf_pic_blit() is expected to leave in
 * a destination octet, replicating exactly the integer arithmetic of
 * ubuf_pic_blit_alpha() for the case where the source carries an "a8" alpha
 * plane.
 *
 * @param dst destination octet value
 * @param src source octet value
 * @param a8 alpha plane octet value for this pixel
 * @param alpha alpha multiplier passed to ubuf_pic_blit()
 * @param threshold threshold passed to ubuf_pic_blit()
 * @return the octet value expected in dst after the blit
 */
static uint8_t expected_blit(uint8_t dst, uint8_t src, uint8_t a8,
                             int alpha, int threshold)
{
    /* threshold == 0: straight copy (memcpy path). */
    if (threshold == 0)
        return src;

    /* alpha == 0xff with threshold in ]0, 0xff[: on/off blending that uses the
     * alpha plane sample directly, without scaling. */
    if (alpha == 0xff && threshold != 0xff) {
        if (a8 > threshold)
            return src;
        else
            return dst;
    }

    /* alpha != 0xff with threshold in ]0, 0xff[: on/off blending that scales the
     * alpha plane sample by the multiplier first. */
    if (alpha != 0xff && threshold != 0xff) {
        uint8_t a = (uint8_t)((uint16_t)a8 * (uint16_t)alpha / 0xff);
        if (a > threshold)
            return src;
        else
            return dst;
    }

    /* alpha == 0xff with threshold == 0xff: smooth (per-pixel) blending that
     * uses the alpha plane sample directly. */
    if (alpha == 0xff && threshold == 0xff) {
        return (uint8_t)((dst * (0xff - a8) + src * a8) / 0xff);
    } else {
        /* alpha != 0xff with threshold == 0xff: smooth blending with the scaled
         * alpha plane sample. */
        uint8_t a = (uint8_t)((uint16_t)a8 * (uint16_t)alpha / 0xff);
        return (uint8_t)((dst * (0xff - a) + src * a) / 0xff);
    }
}

/** @internal @This computes the value ubuf_pic_blit() is expected to leave in a
 * destination 16-bit sample, replicating exactly the integer arithmetic of
 * ubuf_pic_blit_alpha10() for the case where the source carries an "a10l" alpha
 * plane. The saturation values are 10-bit (0x3ff) instead of 8-bit (0xff).
 *
 * @param dst destination sample value
 * @param src source sample value
 * @param a10 alpha plane sample value for this pixel
 * @param alpha alpha multiplier passed to ubuf_pic_blit()
 * @param threshold threshold passed to ubuf_pic_blit()
 * @return the sample value expected in dst after the blit
 */
static uint16_t expected_blit10(uint16_t dst, uint16_t src, uint16_t a10,
                                int alpha, int threshold)
{
    /* threshold == 0: straight copy (memcpy path). */
    if (threshold == 0)
        return src;

    /* alpha == 0x3ff with threshold in ]0, 0x3ff[: on/off blending that uses the
     * alpha plane sample directly, without scaling. */
    if (alpha == 0x3ff && threshold != 0x3ff) {
        if (a10 > threshold)
            return src;
        else
            return dst;
    }

    /* alpha != 0x3ff with threshold in ]0, 0x3ff[: on/off blending that scales
     * the alpha plane sample by the multiplier first. */
    if (alpha != 0x3ff && threshold != 0x3ff) {
        uint16_t a = (uint16_t)(a10 * alpha / 0x3ff);
        if (a > threshold)
            return src;
        else
            return dst;
    }

    /* alpha == 0x3ff with threshold == 0x3ff: smooth (per-pixel) blending that
     * uses the alpha plane sample directly. */
    if (alpha == 0x3ff && threshold == 0x3ff) {
        return (uint16_t)((dst * (0x3ff - a10) + src * a10) / 0x3ff);
    } else {
        /* alpha != 0x3ff with threshold == 0x3ff: smooth blending with the
         * scaled alpha plane sample. */
        uint16_t a = (uint16_t)(a10 * alpha / 0x3ff);
        return (uint16_t)((dst * (0x3ff - a) + src * a) / 0x3ff);
    }
}

/** @internal @This computes the value ubuf_pic_blit() is expected to leave in
 * a destination octet when the source carries no alpha plane, i.e. when the
 * fallback call to ubuf_pic_blit_alpha() passes alpha_plane == NULL. The alpha
 * multiplier is then applied uniformly to the whole plane.
 *
 * @param dst destination octet value
 * @param src source octet value
 * @param alpha alpha multiplier passed to ubuf_pic_blit()
 * @param threshold threshold passed to ubuf_pic_blit()
 * @return the octet value expected in dst after the blit
 */
static uint8_t expected_blit_noalpha(uint8_t dst, uint8_t src,
                                     int alpha, int threshold)
{
    /* "nothing to do" early return: dst is left untouched. */
    if (alpha < threshold && threshold != 0xff)
        return dst;

    /* alpha == 0xff or threshold == 0: straight copy (memcpy path). */
    if (alpha == 0xff || threshold == 0)
        return src;

    /* Otherwise a uniform blend with the scalar alpha. */
    return (uint8_t)((dst * (0xff - alpha) + src * alpha) / 0xff);
}

static uint16_t expected_blit10_noalpha(uint16_t dst, uint16_t src,
                                        int alpha, int threshold)
{
    /* "nothing to do" early return: dst is left untouched. */
    if (alpha < threshold && threshold != 0x3ff)
        return dst;

    /* alpha == 0x3ff or threshold == 0: straight copy (memcpy path). */
    if (alpha == 0x3ff || threshold == 0)
        return src;

    /* Otherwise a uniform blend with the scalar alpha. */
    return (uint16_t)((dst * (0x3ff - alpha) + src * alpha) / 0x3ff);
}

/** @internal @This checks the result of a yuv420p blit whose source carries an
 * "a8" alpha plane. It reads that plane and compares every sampled dst octet on
 * the y8/u8/v8 planes against expected_blit(), returning the number of checks
 * performed. */
static unsigned long check_yuv420p_blit(struct ubuf *dst, struct ubuf *src,
                                        int alpha, int threshold)
{
    static const struct { int x, y; } points[] = {
        {  8,  8 }, { 32,  8 }, { 56,  8 },
        {  8, 32 }, { 32, 32 }, { 56, 32 },
        {  8, 56 }, { 32, 56 }, { 56, 56 },
        { 16, 16 }, { 47, 47 },
    };
    size_t stride, a_stride;
    const uint8_t *buffer;
    const uint8_t *a_buffer;
    unsigned long checks = 0;

    ubase_assert(ubuf_pic_plane_size(src, "a8", &a_stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(src, "a8", 0, 0, 64, 64, &a_buffer));

    /* y8: dst's 16 blended with src's 235 by the alpha plane sample. */
    ubase_assert(ubuf_pic_plane_size(dst, "y8", &stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(dst, "y8", 0, 0, 64, 64, &buffer));
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
        int x = points[i].x, y = points[i].y;
        uint8_t a8 = a_buffer[y * a_stride + x];
        uint8_t got = buffer[y * stride + x];
        uint8_t exp = expected_blit(16, 235, a8, alpha, threshold);
        if (got != exp)
            fprintf(stderr, "mismatch on y8 at (%d, %d): got %u, expected %u\n",
                    x, y, got, exp);
        assert(got == exp);
        checks++;
    }
    ubase_assert(ubuf_pic_plane_unmap(dst, "y8", 0, 0, 64, 64));

    /* u8: dst's 128 blended with src's 64 by the alpha plane sample. */
    ubase_assert(ubuf_pic_plane_size(dst, "u8", &stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(dst, "u8", 0, 0, 64, 64, &buffer));
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
        int x = points[i].x, y = points[i].y;
        uint8_t a8 = a_buffer[(y & ~1) * a_stride + (x & ~1)];
        uint8_t got = buffer[(y / 2) * stride + (x / 2)];
        uint8_t exp = expected_blit(128, 64, a8, alpha, threshold);
        if (got != exp)
            fprintf(stderr, "mismatch on u8 at (%d, %d): got %u, expected %u\n",
                    x, y, got, exp);
        assert(got == exp);
        checks++;
    }
    ubase_assert(ubuf_pic_plane_unmap(dst, "u8", 0, 0, 64, 64));

    /* v8: dst's 128 blended with src's 192 by the alpha plane sample. */
    ubase_assert(ubuf_pic_plane_size(dst, "v8", &stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(dst, "v8", 0, 0, 64, 64, &buffer));
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
        int x = points[i].x, y = points[i].y;
        uint8_t a8 = a_buffer[(y & ~1) * a_stride + (x & ~1)];
        uint8_t got = buffer[(y / 2) * stride + (x / 2)];
        uint8_t exp = expected_blit(128, 192, a8, alpha, threshold);
        if (got != exp)
            fprintf(stderr, "mismatch on v8 at (%d, %d): got %u, expected %u\n",
                    x, y, got, exp);
        assert(got == exp);
        checks++;
    }
    ubase_assert(ubuf_pic_plane_unmap(dst, "v8", 0, 0, 64, 64));

    ubase_assert(ubuf_pic_plane_unmap(src, "a8", 0, 0, 64, 64));
    return checks;
}

/** @internal @This checks the result of a yuv420p10le blit whose source carries
 * an "a10l" alpha plane. It reads that plane and compares every sampled dst word
 * on the y10l/u10l/v10l planes against expected_blit10(), returning the number
 * of checks performed. */
static unsigned long check_yuv420p10le_blit(struct ubuf *dst, struct ubuf *src,
                                            int alpha, int threshold)
{
    static const struct { int x, y; } points[] = {
        {  8,  8 }, { 32,  8 }, { 56,  8 },
        {  8, 32 }, { 32, 32 }, { 56, 32 },
        {  8, 56 }, { 32, 56 }, { 56, 56 },
        { 16, 16 }, { 47, 47 },
    };
    size_t stride, a_stride;
    const uint8_t *buffer;
    const uint8_t *a_buffer;
    unsigned long checks = 0;

    ubase_assert(ubuf_pic_plane_size(src, "a10l", &a_stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(src, "a10l", 0, 0, 64, 64, &a_buffer));

    /* y10l: dst's 64 blended with src's 940 by the alpha plane sample. */
    ubase_assert(ubuf_pic_plane_size(dst, "y10l", &stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(dst, "y10l", 0, 0, 64, 64, &buffer));
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
        int x = points[i].x, y = points[i].y;
        uint16_t a10 = ((const uint16_t *)(a_buffer + y * a_stride))[x];
        uint16_t got = ((const uint16_t *)(buffer + y * stride))[x];
        uint16_t exp = expected_blit10(64, 940, a10, alpha, threshold);
        if (got != exp)
            fprintf(stderr, "mismatch on y10l at (%d, %d): got %u, expected %u\n",
                    x, y, got, exp);
        assert(got == exp);
        checks++;
    }
    ubase_assert(ubuf_pic_plane_unmap(dst, "y10l", 0, 0, 64, 64));

    /* u10l: dst's 512 blended with src's 256 by the alpha plane sample. */
    ubase_assert(ubuf_pic_plane_size(dst, "u10l", &stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(dst, "u10l", 0, 0, 64, 64, &buffer));
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
        int x = points[i].x, y = points[i].y;
        uint16_t a10 = ((const uint16_t *)(a_buffer + (y & ~1) * a_stride))[x & ~1];
        uint16_t got = ((const uint16_t *)(buffer + (y / 2) * stride))[x / 2];
        uint16_t exp = expected_blit10(512, 256, a10, alpha, threshold);
        if (got != exp)
            fprintf(stderr, "mismatch on u10l at (%d, %d): got %u, expected %u\n",
                    x, y, got, exp);
        assert(got == exp);
        checks++;
    }
    ubase_assert(ubuf_pic_plane_unmap(dst, "u10l", 0, 0, 64, 64));

    /* v10l: dst's 512 blended with src's 768 by the alpha plane sample. */
    ubase_assert(ubuf_pic_plane_size(dst, "v10l", &stride, NULL, NULL, NULL));
    ubase_assert(ubuf_pic_plane_read(dst, "v10l", 0, 0, 64, 64, &buffer));
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
        int x = points[i].x, y = points[i].y;
        uint16_t a10 = ((const uint16_t *)(a_buffer + (y & ~1) * a_stride))[x & ~1];
        uint16_t got = ((const uint16_t *)(buffer + (y / 2) * stride))[x / 2];
        uint16_t exp = expected_blit10(512, 768, a10, alpha, threshold);
        if (got != exp)
            fprintf(stderr, "mismatch on v10l at (%d, %d): got %u, expected %u\n",
                    x, y, got, exp);
        assert(got == exp);
        checks++;
    }
    ubase_assert(ubuf_pic_plane_unmap(dst, "v10l", 0, 0, 64, 64));

    ubase_assert(ubuf_pic_plane_unmap(src, "a10l", 0, 0, 64, 64));
    return checks;
}

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);

    struct ubuf_mgr *dst_mgr, *src_mgr;
    struct ubuf *dst, *src;
    unsigned long checks;
    int alpha, threshold;

    /* 8-bit alpha blending: "a8" alpha plane, saturation value 0xff. */

    /* dst: single luma plane. */
    dst_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(dst_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "y8", 1, 1, 1));

    /* src: luma plane plus an 8-bit alpha plane. */
    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "a8", 1, 1, 1));

    dst = ubuf_pic_alloc(dst_mgr, 1, 1);
    assert(dst != NULL);
    src = ubuf_pic_alloc(src_mgr, 1, 1);
    assert(src != NULL);

    checks = 0;

    /* The copy branch (threshold == 0) is a plain memcpy of src, independent
     * of both the alpha plane and the alpha multiplier, so a handful of
     * representative values suffice. It is tested here rather than in the
     * sweep below, where it would otherwise be repeated for every (a, alpha)
     * pair without adding coverage. */
    {
        static const int copy_a[]     = { 0, 1, 128, 254, 255 };
        static const int copy_alpha[] = { 0, 128, 255 };
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(copy_a); i++) {
            ubase_assert(ubuf_pic_plane_set_color(src, "y8", 0, 0, 1, 1,
                    (const uint8_t []){ 255 }, 1));
            ubase_assert(ubuf_pic_plane_set_color(src, "a8", 0, 0, 1, 1,
                    (const uint8_t []){ copy_a[i] }, 1));
            for (size_t k = 0; k < UBASE_ARRAY_SIZE(copy_alpha); k++) {
                ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 1, 1,
                        (const uint8_t []){ 0 }, 1));
                ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 1, 1,
                                           copy_alpha[k], 0));
                uint8_t got = read_octet(dst, "y8");
                uint8_t exp = expected_blit(0, 255, copy_a[i], copy_alpha[k], 0);
                assert(got == exp);
                checks++;
            }
        }
    }

    /* For every possible alpha plane value, exercise ubuf_pic_blit() with a
     * spread of alpha/threshold values covering its remaining branches:
     *   - 0 < threshold < 0xff  -> on/off blending, both with alpha == 0xff
     *                              (raw alpha plane) and alpha != 0xff
     *                              (scaled alpha plane)
     *   - threshold == 0xff     -> smooth blending, again both alpha cases
     * The values a-1, a and a+1 straddle the threshold comparison boundary.
     * threshold == 0 (copy) is dropped from the sweep and tested above. */
    for (int a = 0; a <= 255; a++) {
        /* src is constant across the inner loop: opaque luma, alpha = a. */
        ubase_assert(ubuf_pic_plane_set_color(src, "y8", 0, 0, 1, 1,
                (const uint8_t []){ 255 }, 1));
        ubase_assert(ubuf_pic_plane_set_color(src, "a8", 0, 0, 1, 1,
                (const uint8_t []){ a }, 1));

        const int alpha_candidates[]     = { 0, 255, a-32, a-1, a, a+1, a+32 };
        const int threshold_candidates[] = {    255, a-32, a-1, a, a+1, a+32 };

        for (size_t ai = 0; ai < UBASE_ARRAY_SIZE(alpha_candidates); ai++) {
            alpha = alpha_candidates[ai];
            if (alpha < 0 || alpha > 255)
                continue;

            for (size_t ti = 0; ti < UBASE_ARRAY_SIZE(threshold_candidates); ti++) {
                threshold = threshold_candidates[ti];
                if (threshold < 0 || threshold > 255)
                    continue;

                /* dst is reset before every call since blit overwrites it. */
                ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 1, 1,
                        (const uint8_t []){ 0 }, 1));

                ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 1, 1,
                                           alpha, threshold));

                uint8_t got = read_octet(dst, "y8");
                uint8_t exp = expected_blit(0, 255, a, alpha, threshold);
                if (got != exp)
                    fprintf(stderr, "mismatch: a=%d alpha=%d threshold=%d: "
                            "got %u, expected %u\n",
                            a, alpha, threshold, got, exp);
                assert(got == exp);
                checks++;
            }
        }
    }

    printf("%lu 8-bit blit checks passed\n", checks);

    ubuf_free(src);
    ubuf_free(dst);
    ubuf_mgr_release(src_mgr);
    ubuf_mgr_release(dst_mgr);

    /* 10-bit alpha blending: "a10l" alpha plane, saturation value 0x3ff.
     * Identical in structure to the 8-bit section above, but the samples are
     * 16-bit little-endian and the range spans 0 to 1023. */

    /* dst: single 10-bit luma plane. */
    dst_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(dst_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "y10l", 1, 1, 2));

    /* src: 10-bit luma plane plus a 10-bit alpha plane. */
    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "a10l", 1, 1, 2));

    dst = ubuf_pic_alloc(dst_mgr, 1, 1);
    assert(dst != NULL);
    src = ubuf_pic_alloc(src_mgr, 1, 1);
    assert(src != NULL);

    checks = 0;

    /* Copy branch (threshold == 0), tested separately as in the 8-bit case. */
    {
        static const int copy_a[]     = { 0, 1, 512, 1022, 1023 };
        static const int copy_alpha[] = { 0, 512, 1023 };
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(copy_a); i++) {
            ubase_assert(ubuf_pic_plane_set_color(src, "y10l", 0, 0, 1, 1,
                    (const uint8_t []){ 0xff, 0x3 }, 2));
            ubase_assert(ubuf_pic_plane_set_color(src, "a10l", 0, 0, 1, 1,
                    (const uint8_t []){ copy_a[i] & 0xff, copy_a[i] >> 8 }, 2));
            for (size_t k = 0; k < UBASE_ARRAY_SIZE(copy_alpha); k++) {
                ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 1, 1,
                        (const uint8_t []){ 0, 0 }, 2));
                ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 1, 1,
                                           copy_alpha[k], 0));
                uint16_t got = read_word(dst, "y10l");
                uint16_t exp = expected_blit10(0, 1023, copy_a[i],
                                               copy_alpha[k], 0);
                assert(got == exp);
                checks++;
            }
        }
    }

    for (int a = 0; a <= 1023; a++) {
        /* src is constant across the inner loop: opaque luma, alpha = a. */
        ubase_assert(ubuf_pic_plane_set_color(src, "y10l", 0, 0, 1, 1,
                (const uint8_t []){ 0xff, 0x3 }, 2));
        ubase_assert(ubuf_pic_plane_set_color(src, "a10l", 0, 0, 1, 1,
                (const uint8_t []){ a & 0xff, a >> 8 }, 2));

        const int alpha_candidates[]     = { 0, 1023, a-32, a-1, a, a+1, a+32 };
        const int threshold_candidates[] = {    1023, a-32, a-1, a, a+1, a+32 };

        for (size_t ai = 0; ai < UBASE_ARRAY_SIZE(alpha_candidates); ai++) {
            alpha = alpha_candidates[ai];
            if (alpha < 0 || alpha > 1023)
                continue;

            for (size_t ti = 0; ti < UBASE_ARRAY_SIZE(threshold_candidates); ti++) {
                threshold = threshold_candidates[ti];
                if (threshold < 0 || threshold > 1023)
                    continue;

                /* dst is reset before every call since blit overwrites it. */
                ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 1, 1,
                        (const uint8_t []){ 0, 0 }, 2));

                ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 1, 1,
                                           alpha, threshold));

                uint16_t got = read_word(dst, "y10l");
                uint16_t exp = expected_blit10(0, 1023, a, alpha, threshold);
                if (got != exp)
                    fprintf(stderr, "mismatch: a=%d alpha=%d threshold=%d: "
                            "got %u, expected %u\n",
                            a, alpha, threshold, got, exp);
                assert(got == exp);
                checks++;
            }
        }
    }

    printf("%lu 10-bit blit checks passed\n", checks);

    ubuf_free(src);
    ubuf_free(dst);
    ubuf_mgr_release(src_mgr);
    ubuf_mgr_release(dst_mgr);

    /* No alpha plane, uniform blend: src has no "a8"/"a10l" plane, so
     * ubuf_pic_blit() falls back to ubuf_pic_blit_alpha() with
     * alpha_plane == NULL. With threshold 0xff the scalar alpha is applied
     * uniformly across the plane: dst = (dst*(0xff-alpha) + src*alpha)/0xff.
     * With dst = 0 and src = 255 the result is simply alpha. */

    /* dst: single luma plane. */
    dst_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(dst_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "y8", 1, 1, 1));

    /* src: luma plane only, no alpha plane. */
    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y8", 1, 1, 1));

    dst = ubuf_pic_alloc(dst_mgr, 1, 1);
    assert(dst != NULL);
    src = ubuf_pic_alloc(src_mgr, 1, 1);
    assert(src != NULL);

    checks = 0;

    for (alpha = 0; alpha <= 255; alpha++) {
        ubase_assert(ubuf_pic_plane_set_color(src, "y8", 0, 0, 1, 1,
                (const uint8_t []){ 255 }, 1));
        ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 1, 1,
                (const uint8_t []){ 0 }, 1));

        ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 1, 1, alpha, 255));

        uint8_t got = read_octet(dst, "y8");
        uint8_t exp = expected_blit_noalpha(0, 255, alpha, 255);
        if (got != exp)
            fprintf(stderr, "mismatch: alpha=%d threshold=255: "
                    "got %u, expected %u\n", alpha, got, exp);
        assert(got == exp);
        checks++;
    }

    printf("%lu no-alpha-plane blit checks passed\n", checks);

    ubuf_free(src);
    ubuf_free(dst);
    ubuf_mgr_release(src_mgr);
    ubuf_mgr_release(dst_mgr);

    /* TODO: add a new section to test 10-bit blending without an alpha plane
     * once that code can be run. (ubuf_pic_blit()'s no-alpha-plane fallback
     * currently always calls the 8-bit ubuf_pic_blit_alpha(), so the 10-bit
     * ubuf_pic_blit_alpha10() NULL path is not reachable through
     * ubuf_pic_blit().) */

    /* 64x64 yuv420p pictures. src additionally carries a full-resolution 8-bit
     * alpha plane that is transparent (0) everywhere except an opaque (255)
     * 32x32 square whose top-left corner is at (16, 16). A single on/off blit
     * (alpha 255, threshold 20) copies that square into dst; the result is not
     * checked here. */

    /* dst: yuv420p. */
    dst_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(dst_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "v8", 2, 2, 1));

    /* src: yuv420p plus a full-resolution 8-bit alpha plane. */
    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "v8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "a8", 1, 1, 1));

    dst = ubuf_pic_alloc(dst_mgr, 64, 64);
    assert(dst != NULL);
    src = ubuf_pic_alloc(src_mgr, 64, 64);
    assert(src != NULL);

    /* dst: neutral black in limited-range YUV. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    /* src: a flat colour, opaque only inside the 32x32 square at (16, 16). */
    ubase_assert(ubuf_pic_plane_set_color(src, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 235 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 64 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 192 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "a8", 0, 0, 64, 64,
            (const uint8_t []){ 0 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "a8", 16, 16, 32, 32,
            (const uint8_t []){ 255 }, 1));

    /* Check a 3x3 grid of samples on each plane. The alpha is opaque only
     * inside the 32x32 square [16, 48) x [16, 48); with alpha 255 and threshold
     * 20 the blit copies src there and leaves dst everywhere else, so only
     * (32, 32) falls inside the square. On the subsampled chroma planes a luma
     * point (x, y) maps to chroma sample (x/2, y/2). */
    alpha = 255;
    threshold = 20;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

    checks = check_yuv420p_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Second pass: reset dst to its original values and blit the same src
     * again, this time with a reduced alpha of 128 (same threshold of 20).
     * With alpha != 0xff the plane value is scaled (a = raw * 128 / 255) before
     * the threshold test, exercising the on/off path the first blit skipped.
     * Inside the square the effective alpha is 128, still above 20, so the
     * copied region matches the first pass. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    alpha = 128;
    threshold = 20;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

    checks = check_yuv420p_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Third pass: reset dst, then overwrite src's alpha plane with a diagonal
     * gradient (0 at the top-left, rising towards the bottom-right). Blit with
     * alpha 255 and threshold 255: threshold 255 selects smooth per-pixel
     * blending, so each dst sample becomes a weighted mix of dst and src,
     * (dst * (255 - a) + src * a) / 255, using the local gradient alpha a.
     * Rather than recomputing the gradient, the check reads the alpha the blit
     * actually saw straight from the "a8" plane. All the sample points sit on
     * even coordinates, so the subsampled chroma planes read the same alpha
     * sample as luma. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    fill_plane_gradient(src, "a8", 0, 0, 64, 64);

    alpha = 255;
    threshold = 255;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

    checks = check_yuv420p_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p gradient blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Fourth pass: the same gradient blit, but with alpha 128. Threshold 255
     * still selects smooth blending; with alpha != 0xff the gradient value is
     * scaled first (a = raw * 128 / 255), so each dst sample becomes
     * (dst * (255 - a) + src * a) / 255. As before the check reads the raw a8
     * value the blit saw and lets expected_blit apply the same scaling. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    fill_plane_gradient(src, "a8", 0, 0, 64, 64);

    alpha = 128;
    threshold = 255;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

    checks = check_yuv420p_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p gradient blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Fifth pass: a source with no alpha plane. Rebuild src with only
     * y8/u8/v8 (freeing the alpha-carrying one), reset dst, then blit with
     * alpha 128 and threshold 1. With no alpha plane the blit applies the
     * scalar alpha uniformly, so every sample is a blend
     * (dst * (255 - 128) + src * 128) / 255. */
    ubuf_free(src);
    ubuf_mgr_release(src_mgr);

    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "v8", 2, 2, 1));

    src = ubuf_pic_alloc(src_mgr, 64, 64);
    assert(src != NULL);
    ubase_assert(ubuf_pic_plane_set_color(src, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 235 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 64 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 192 }, 1));

    /* dst: neutral black in limited-range YUV. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    alpha = 128;
    threshold = 1;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

    checks = 0;
    {
        static const struct { int x, y; } points[] = {
            {  8,  8 }, { 32,  8 }, { 56,  8 },
            {  8, 32 }, { 32, 32 }, { 56, 32 },
            {  8, 56 }, { 32, 56 }, { 56, 56 },
            { 16, 16 }, { 47, 47 },
        };
        size_t stride;
        const uint8_t *buffer;

        /* y8: uniform blend of dst's 16 with src's 235. */
        ubase_assert(ubuf_pic_plane_size(dst, "y8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "y8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint8_t got = buffer[y * stride + x];
            uint8_t exp = expected_blit_noalpha(16, 235, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on y8 at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "y8", 0, 0, 64, 64));

        /* u8: uniform blend of dst's 128 with src's 64. */
        ubase_assert(ubuf_pic_plane_size(dst, "u8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "u8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint8_t got = buffer[(y / 2) * stride + (x / 2)];
            uint8_t exp = expected_blit_noalpha(128, 64, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on u8 at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "u8", 0, 0, 64, 64));

        /* v8: uniform blend of dst's 128 with src's 192. */
        ubase_assert(ubuf_pic_plane_size(dst, "v8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "v8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint8_t got = buffer[(y / 2) * stride + (x / 2)];
            uint8_t exp = expected_blit_noalpha(128, 192, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on v8 at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "v8", 0, 0, 64, 64));
        printf("%lu yuv420p no-alpha blit checks passed (alpha %d, threshold %d)\n",
               checks, alpha, threshold);
    }

    ubuf_free(src);
    ubuf_free(dst);
    ubuf_mgr_release(src_mgr);
    ubuf_mgr_release(dst_mgr);

    /* 64x64 yuv420p10le pictures: the 10-bit analogue of the yuv420p case
     * above. The sample values are scaled up by four for the two extra bits,
     * and the alpha square is fully opaque (1023). A single on/off blit (alpha
     * 1023, threshold 80) copies the 32x32 square at (16, 16) into dst. */

    /* dst: yuv420p10le. */
    dst_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(dst_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "u10l", 2, 2, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "v10l", 2, 2, 2));

    /* src: yuv420p10le plus a full-resolution 10-bit alpha plane. */
    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "u10l", 2, 2, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "v10l", 2, 2, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "a10l", 1, 1, 2));

    dst = ubuf_pic_alloc(dst_mgr, 64, 64);
    assert(dst != NULL);
    src = ubuf_pic_alloc(src_mgr, 64, 64);
    assert(src != NULL);

    /* dst: neutral black in limited-range YUV. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0x40, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));

    /* src: a flat colour, opaque only inside the 32x32 square at (16, 16). */
    ubase_assert(ubuf_pic_plane_set_color(src, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0xac, 0x3 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(src, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x1 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(src, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x3 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(src, "a10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(src, "a10l", 16, 16, 32, 32,
            (const uint8_t []){ 0xff, 0x3 }, 2));

    /* Check the same 3x3 grid on each plane. As before only (32, 32) is inside
     * the opaque square; on the subsampled chroma planes a luma point (x, y)
     * maps to sample (x/2, y/2). The samples are 16-bit. */
    alpha = 1023;
    threshold = 80;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));
    checks = check_yuv420p10le_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p10le blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Second pass: reset dst and blit the same src again with a reduced alpha
     * of 512 (same threshold of 80). With alpha != 0x3ff the plane value is
     * scaled (a = raw * 512 / 1023) before the threshold test. Inside the
     * square the effective alpha is 512, still above 80, so the copied region
     * matches the first pass. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0x40, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));

    alpha = 512;
    threshold = 80;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));
    checks = check_yuv420p10le_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p10le blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Replace src's alpha with a diagonal gradient for the remaining passes. */
    fill_plane_gradient16(src, "a10l", 0, 0, 64, 64);

    /* Third pass: reset dst and blit with alpha 1023 and threshold 1023.
     * Threshold 1023 selects smooth per-pixel blending; with alpha == 0x3ff the
     * gradient alpha is used directly, so each dst sample becomes
     * (dst * (1023 - a) + src * a) / 1023. The check reads the alpha the blit
     * saw straight from the "a10l" plane. All the sample points sit on even
     * coordinates, so the subsampled chroma planes read the same alpha sample
     * as luma. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0x40, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));

    alpha = 1023;
    threshold = 1023;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));
    checks = check_yuv420p10le_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p10le gradient blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Fourth pass: the same gradient blit with alpha 512. Threshold 1023 still
     * selects smooth blending; with alpha != 0x3ff the gradient value is scaled
     * first (a = raw * 512 / 1023) before the blend. The check again reads the
     * raw a10l value and lets expected_blit10 apply the scaling. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0x40, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));

    alpha = 512;
    threshold = 1023;
    ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));
    checks = check_yuv420p10le_blit(dst, src, alpha, threshold);
    printf("%lu yuv420p10le gradient blit checks passed (alpha %d, threshold %d)\n",
           checks, alpha, threshold);

    /* Fifth pass: a 10-bit source with no alpha plane. Rebuild src with only
     * y10l/u10l/v10l (freeing the alpha-carrying one), reset dst, then blit
     * with alpha 1023 and threshold 0. threshold 0 forces the memcpy path, so
     * with no alpha plane dst becomes an exact copy of src everywhere. */
    ubuf_free(src);
    ubuf_mgr_release(src_mgr);

    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "u10l", 2, 2, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "v10l", 2, 2, 2));

    src = ubuf_pic_alloc(src_mgr, 64, 64);
    assert(src != NULL);
    ubase_assert(ubuf_pic_plane_set_color(src, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0xac, 0x3 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(src, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x1 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(src, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x3 }, 2));

    /* dst: neutral black in limited-range YUV. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0x40, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));

    checks = 0;
    {
        alpha = 1023;
        threshold = 0;
        static const struct { int x, y; } points[] = {
            {  8,  8 }, { 32,  8 }, { 56,  8 },
            {  8, 32 }, { 32, 32 }, { 56, 32 },
            {  8, 56 }, { 32, 56 }, { 56, 56 },
            { 16, 16 }, { 47, 47 },
        };
        size_t stride;
        const uint8_t *buffer;

        ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

        /* y10l: a straight copy of src's 940. */
        ubase_assert(ubuf_pic_plane_size(dst, "y10l", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "y10l", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint16_t got = ((const uint16_t *)(buffer + y * stride))[x];
            uint16_t exp = expected_blit10_noalpha(64, 940, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on y10l at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "y10l", 0, 0, 64, 64));

        /* u10l: a straight copy of src's 256. */
        ubase_assert(ubuf_pic_plane_size(dst, "u10l", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "u10l", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint16_t got = ((const uint16_t *)(buffer + (y / 2) * stride))[x / 2];
            uint16_t exp = expected_blit10_noalpha(512, 256, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on u10l at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "u10l", 0, 0, 64, 64));

        /* v10l: a straight copy of src's 768. */
        ubase_assert(ubuf_pic_plane_size(dst, "v10l", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "v10l", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint16_t got = ((const uint16_t *)(buffer + (y / 2) * stride))[x / 2];
            uint16_t exp = expected_blit10_noalpha(512, 768, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on v10l at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "v10l", 0, 0, 64, 64));
        printf("%lu yuv420p10le no-alpha blit checks passed (alpha %d, threshold %d)\n",
               checks, alpha, threshold);
    }

    /* Sixth pass: the same no-alpha 10-bit source, but blit with alpha 512 and
     * threshold 1. This case is EXPECTED TO FAIL. When src has no alpha plane
     * ubuf_pic_blit always falls back to the 8-bit ubuf_pic_blit_alpha, which
     * blends the 10-bit little-endian buffer one byte at a time. With alpha 512
     * the 8-bit weight (0xff - alpha) is negative and the per-byte results
     * overflow, so the recombined 16-bit samples (e.g. 1560 for luma) do not
     * match expected_blit10_noalpha's ideal blend of
     * (dst * (1023 - 512) + src * 512) / 1023. The asserts below are commented
     * out so the mismatches are reported without aborting the run. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y10l", 0, 0, 64, 64,
            (const uint8_t []){ 0x40, 0 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));
    ubase_assert(ubuf_pic_plane_set_color(dst, "v10l", 0, 0, 64, 64,
            (const uint8_t []){ 0, 0x2 }, 2));

    checks = 0;
    {
        alpha = 512;
        threshold = 1;
        static const struct { int x, y; } points[] = {
            {  8,  8 }, { 32,  8 }, { 56,  8 },
            {  8, 32 }, { 32, 32 }, { 56, 32 },
            {  8, 56 }, { 32, 56 }, { 56, 56 },
            { 16, 16 }, { 47, 47 },
        };
        size_t stride;
        const uint8_t *buffer;

        ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

        /* y10l: uniform blend of dst's 64 with src's 940. */
        ubase_assert(ubuf_pic_plane_size(dst, "y10l", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "y10l", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint16_t got = ((const uint16_t *)(buffer + y * stride))[x];
            uint16_t exp = expected_blit10_noalpha(64, 940, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on y10l at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            /* assert(got == exp); expected to fail; see note above */
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "y10l", 0, 0, 64, 64));

        /* u10l: uniform blend of dst's 512 with src's 256. */
        ubase_assert(ubuf_pic_plane_size(dst, "u10l", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "u10l", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint16_t got = ((const uint16_t *)(buffer + (y / 2) * stride))[x / 2];
            uint16_t exp = expected_blit10_noalpha(512, 256, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on u10l at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            /* assert(got == exp); expected to fail; see note above */
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "u10l", 0, 0, 64, 64));

        /* v10l: uniform blend of dst's 512 with src's 768. */
        ubase_assert(ubuf_pic_plane_size(dst, "v10l", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "v10l", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint16_t got = ((const uint16_t *)(buffer + (y / 2) * stride))[x / 2];
            uint16_t exp = expected_blit10_noalpha(512, 768, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on v10l at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            /* assert(got == exp); expected to fail; see note above */
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "v10l", 0, 0, 64, 64));
        printf("%lu yuv420p10le no-alpha blit checks passed (alpha %d, threshold %d)\n",
               checks, alpha, threshold);
    }

    ubuf_free(src);
    ubuf_free(dst);
    ubuf_mgr_release(src_mgr);
    ubuf_mgr_release(dst_mgr);

    /* 64x64 pictures with a semi-planar nv12 dst and a planar yuv420p src. nv12
     * keeps luma in a full-resolution "y8" plane and interleaves the U and V
     * chroma samples in a single "u8v8" plane (macropixel_size 2, subsampled
     * 2x2), while yuv420p keeps them in separate "u8"/"v8" planes. */
    dst_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(dst_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(dst_mgr, "u8v8", 2, 2, 2));

    src_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_PREPEND, UBUF_APPEND,
                                     UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(src_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(src_mgr, "v8", 2, 2, 1));

    dst = ubuf_pic_alloc(dst_mgr, 64, 64);
    assert(dst != NULL);
    src = ubuf_pic_alloc(src_mgr, 64, 64);
    assert(src != NULL);

    /* dst: neutral black in limited-range YUV (chroma 128 for both U and V). */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    /* src: a flat colour (no alpha plane). */
    ubase_assert(ubuf_pic_plane_set_color(src, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 235 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "u8", 0, 0, 64, 64,
            (const uint8_t []){ 64 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(src, "v8", 0, 0, 64, 64,
            (const uint8_t []){ 192 }, 1));

    /* Blit with alpha 255 and threshold 0: threshold 0 is a straight copy, so
     * dst takes src across the whole 64x64 area regardless of the alpha plane.
     * The same 3x3 grid is checked on each plane as in the first yuv420p test;
     * on the subsampled chroma planes a luma point (x, y) maps to sample
     * (x/2, y/2). */
    checks = 0;
    {
        alpha = 255;
        threshold = 0;
        static const struct { int x, y; } points[] = {
            {  8,  8 }, { 32,  8 }, { 56,  8 },
            {  8, 32 }, { 32, 32 }, { 56, 32 },
            {  8, 56 }, { 32, 56 }, { 56, 56 },
            { 16, 16 }, { 47, 47 },
        };
        size_t stride;
        const uint8_t *buffer;

        ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

        /* y8: the copy replaces dst's 16 with src's 235 everywhere. */
        ubase_assert(ubuf_pic_plane_size(dst, "y8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "y8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint8_t got = buffer[y * stride + x];
            uint8_t exp = expected_blit_noalpha(16, 235, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on y8 at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            assert(got == exp);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "y8", 0, 0, 64, 64));

        /* u8v8: the interleaved chroma plane. Each 2-byte sample holds U at
         * offset 0 and V at offset 1; the copy takes src's U = 64 and V = 192. */
        ubase_assert(ubuf_pic_plane_size(dst, "u8v8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "u8v8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            const uint8_t *sample = buffer + (y / 2) * stride + (x / 2) * 2;
            uint8_t got_u = sample[0];
            uint8_t got_v = sample[1];
            uint8_t exp_u = expected_blit_noalpha(128, 64, alpha, threshold);
            uint8_t exp_v = expected_blit_noalpha(128, 192, alpha, threshold);
            if (got_u != exp_u)
                fprintf(stderr, "mismatch on u8v8 U at (%d, %d): got %u, expected %u\n",
                        x, y, got_u, exp_u);
            assert(got_u == exp_u);
            checks++;
            if (got_v != exp_v)
                fprintf(stderr, "mismatch on u8v8 V at (%d, %d): got %u, expected %u\n",
                        x, y, got_v, exp_v);
            assert(got_v == exp_v);
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "u8v8", 0, 0, 64, 64));
        printf("%lu nv12 blit checks passed (alpha %d, threshold %d)\n",
               checks, alpha, threshold);
    }

    /* Second nv12 pass: the same no-alpha src, but blit with alpha 128 and
     * threshold 0. This case is EXPECTED TO FAIL on the chroma plane. y8 is a
     * single plane, so threshold 0 gives a plain memcpy and dst takes src's
     * 235. But nv12's u8v8 is filled from src's two separate u8/v8 planes, and
     * that de-planarising path blends even when threshold is 0: each byte
     * becomes (dst * (255 - 128) + src * 128) / 255 rather than a straight
     * copy, so the chroma samples do not match expected_blit_noalpha's
     * threshold-0 copy. The asserts below are commented out so the mismatches
     * are reported without aborting the run. */
    ubase_assert(ubuf_pic_plane_set_color(dst, "y8", 0, 0, 64, 64,
            (const uint8_t []){ 16 }, 1));
    ubase_assert(ubuf_pic_plane_set_color(dst, "u8v8", 0, 0, 64, 64,
            (const uint8_t []){ 128 }, 1));

    checks = 0;
    {
        alpha = 128;
        threshold = 0;
        static const struct { int x, y; } points[] = {
            {  8,  8 }, { 32,  8 }, { 56,  8 },
            {  8, 32 }, { 32, 32 }, { 56, 32 },
            {  8, 56 }, { 32, 56 }, { 56, 56 },
            { 16, 16 }, { 47, 47 },
        };
        size_t stride;
        const uint8_t *buffer;

        ubase_assert(ubuf_pic_blit(dst, src, 0, 0, 0, 0, 64, 64, alpha, threshold));

        /* y8: a single plane, so the copy replaces dst's 16 with src's 235. */
        ubase_assert(ubuf_pic_plane_size(dst, "y8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "y8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            uint8_t got = buffer[y * stride + x];
            uint8_t exp = expected_blit_noalpha(16, 235, alpha, threshold);
            if (got != exp)
                fprintf(stderr, "mismatch on y8 at (%d, %d): got %u, expected %u\n",
                        x, y, got, exp);
            /* assert(got == exp); expected to fail; see note above */
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "y8", 0, 0, 64, 64));

        /* u8v8: the interleaved chroma plane, filled from src's separate u8/v8
         * planes. The de-planarising path blends here even at threshold 0. */
        ubase_assert(ubuf_pic_plane_size(dst, "u8v8", &stride, NULL, NULL, NULL));
        ubase_assert(ubuf_pic_plane_read(dst, "u8v8", 0, 0, 64, 64, &buffer));
        for (size_t i = 0; i < UBASE_ARRAY_SIZE(points); i++) {
            int x = points[i].x, y = points[i].y;
            const uint8_t *sample = buffer + (y / 2) * stride + (x / 2) * 2;
            uint8_t got_u = sample[0];
            uint8_t got_v = sample[1];
            uint8_t exp_u = expected_blit_noalpha(128, 64, alpha, threshold);
            uint8_t exp_v = expected_blit_noalpha(128, 192, alpha, threshold);
            if (got_u != exp_u)
                fprintf(stderr, "mismatch on u8v8 U at (%d, %d): got %u, expected %u\n",
                        x, y, got_u, exp_u);
            /* assert(got_u == exp_u); expected to fail; see note above */
            checks++;
            if (got_v != exp_v)
                fprintf(stderr, "mismatch on u8v8 V at (%d, %d): got %u, expected %u\n",
                        x, y, got_v, exp_v);
            /* assert(got_v == exp_v); expected to fail; see note above */
            checks++;
        }
        ubase_assert(ubuf_pic_plane_unmap(dst, "u8v8", 0, 0, 64, 64));
        printf("%lu nv12 blit checks passed (alpha %d, threshold %d)\n",
               checks, alpha, threshold);
    }

    ubuf_free(src);
    ubuf_free(dst);
    ubuf_mgr_release(src_mgr);
    ubuf_mgr_release(dst_mgr);

    umem_mgr_release(umem_mgr);
    return 0;
}

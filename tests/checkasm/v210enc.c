/*
 * Copyright (c) 2015 Henrik Gramner
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include <libavutil/intreadwrite.h>

#include "checkasm.h"
#include "lib/upipe-v210/v210enc.h"

#define BUF_SIZE 512

#define randomize_buffers(mask)                        \
    do {                                               \
        int i, size = sizeof(*y0);                     \
        for (i = 0; i < BUF_SIZE; i += 4 / size) {     \
            uint32_t r = rnd() & mask;                 \
            AV_WN32A(y0 + i, r);                       \
            AV_WN32A(y1 + i, r);                       \
        }                                              \
        for (i = 0; i < BUF_SIZE / 2; i += 4 / size) { \
            uint32_t r = rnd() & mask;                 \
            AV_WN32A(u0 + i, r);                       \
            AV_WN32A(u1 + i, r);                       \
            r = rnd() & mask;                          \
            AV_WN32A(v0 + i, r);                       \
            AV_WN32A(v1 + i, r);                       \
        }                                              \
        for (i = 0; i < width * 8 / 3; i += 4) {       \
            uint32_t r = rnd();                        \
            AV_WN32A(dst0 + i, r);                     \
            AV_WN32A(dst1 + i, r);                     \
        }                                              \
    } while (0)

#define check_pack_line(type, mask)                                                \
    do {                                                                           \
        type y0[BUF_SIZE];                                                         \
        type y1[BUF_SIZE];                                                         \
        type u0[BUF_SIZE / 2];                                                     \
        type u1[BUF_SIZE / 2];                                                     \
        type v0[BUF_SIZE / 2];                                                     \
        type v1[BUF_SIZE / 2];                                                     \
        uint8_t dst0[BUF_SIZE * 8 / 3];                                            \
        uint8_t dst1[BUF_SIZE * 8 / 3];                                            \
                                                                                   \
        declare_func(void, const type * y, const type * u, const type * v,         \
                     uint8_t * dst, ptrdiff_t width);                              \
        ptrdiff_t width, step = 12 / sizeof(type);                                 \
                                                                                   \
        for (width = step; width < BUF_SIZE - 15; width += step) {                 \
            int y_offset  = rnd() & 15;                                            \
            int uv_offset = y_offset / 2;                                          \
            randomize_buffers(mask);                                               \
            call_ref(y0 + y_offset, u0 + uv_offset, v0 + uv_offset, dst0, width);  \
            call_new(y1 + y_offset, u1 + uv_offset, v1 + uv_offset, dst1, width);  \
            if (memcmp(y0, y1, BUF_SIZE) || memcmp(u0, u1, BUF_SIZE / 2) ||        \
                memcmp(v0, v1, BUF_SIZE / 2) || memcmp(dst0, dst1, width * 8 / 3)) \
                fail();                                                            \
            bench_new(y1 + y_offset, u1 + uv_offset, v1 + uv_offset, dst1, width); \
        }                                                                          \
    } while (0)

void checkasm_check_v210enc(void)
{
    struct {
        void (*planar_10)(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t width);
        void (*planar_8)(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst, ptrdiff_t width);
    } s = {
        .planar_10 = upipe_planar_to_v210_10_c,
        .planar_8  = upipe_planar_to_v210_8_c,
    };

#ifdef HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.planar_10 = upipe_planar_to_v210_10_ssse3;
        s.planar_8  = upipe_planar_to_v210_8_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX)
        s.planar_8  = upipe_planar_to_v210_8_avx;
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.planar_10 = upipe_planar_to_v210_10_avx2;
        s.planar_8  = upipe_planar_to_v210_8_avx2;
    }
#endif

    if (check_func(s.planar_8, "planar_to_v210_8"))
        check_pack_line(uint8_t, 0xffffffff);
    report("planar_to_v210_8");

    if (check_func(s.planar_10, "planar_to_v210_10"))
        check_pack_line(uint16_t, 0x03ff03ff);
    report("planar_to_v210_10");
}

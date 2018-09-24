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
#include <libavutil/mem.h>

#include "checkasm.h"
#include "lib/upipe-v210/v210dec.h"

static uint32_t clip(uint32_t value)
{
    if (value < 4)
        return 4;
    if (value > 1019)
        return 1019;
    return value;
}

static void write_v210(void *src0, void *src1)
{
    uint32_t t0 = rnd() & 0x3ff,
             t1 = rnd() & 0x3ff,
             t2 = rnd() & 0x3ff;
    uint32_t value =  clip(t0)
                   | (clip(t1) << 10)
                   | (clip(t2) << 20);
    AV_WL32(src0, value);
    AV_WL32(src1, value);
}

#define BUF_SIZE 512

static void randomize_buffers(void *src0, void *src1)
{
    for (int i = 0; i < BUF_SIZE * 8 / 3 / 4; i++) {
        write_v210(src0, src1);
        src0 += 4;
        src1 += 4;
    }
}

#define declare(type) \
        type y0[BUF_SIZE]; \
        type y1[BUF_SIZE]; \
        type u0[BUF_SIZE / 2]; \
        type u1[BUF_SIZE / 2]; \
        type v0[BUF_SIZE / 2]; \
        type v1[BUF_SIZE / 2]; \
        DECLARE_ALIGNED(32, uint32_t, src0)[BUF_SIZE * 8 / 3 / 4]; \
        DECLARE_ALIGNED(32, uint32_t, src1)[BUF_SIZE * 8 / 3 / 4]; \
        declare_func(void, const void *src, type *y, type *u, type *v, ptrdiff_t width); \
        ptrdiff_t width, step = 12 / sizeof(type)

void checkasm_check_v210dec(void)
{
    struct {
        void (*planar_10)(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);
        void (*planar_8)(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
    } s = {
        .planar_10 = upipe_v210_to_planar_10_c,
        .planar_8  = upipe_v210_to_planar_8_c,
    };

#ifdef HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.planar_10 = upipe_v210_to_planar_10_ssse3;
        s.planar_8  = upipe_v210_to_planar_8_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
        s.planar_10 = upipe_v210_to_planar_10_avx;
        s.planar_8  = upipe_v210_to_planar_8_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.planar_10 = upipe_v210_to_planar_10_avx2;
        s.planar_8  = upipe_v210_to_planar_8_avx2;
    }
#endif

    if (check_func(s.planar_8, "v210_to_planar8")) {
        declare(uint8_t);
        for (width = step; width < BUF_SIZE - 15; width += step) {
            randomize_buffers(src0, src1);
            call_ref(src0, y0, u0, v0, width);
            call_new(src1, y1, u1, v1, width);
            if (memcmp(y0, y1, width) || memcmp(u0, u1, width / 2) || memcmp(v0, v1, width / 2))
                fail();
            bench_new(src1, y1, u1, v1, width);
        }
    }
    report("v210_to_planar8");

    if (check_func(s.planar_10, "v210_to_planar10")) {
        declare(uint16_t);
        for (width = step; width < BUF_SIZE - 15; width += step) {
            randomize_buffers(src0, src1);
            call_ref(src0, y0, u0, v0, width);
            call_new(src1, y1, u1, v1, width);
            if (memcmp(y0, y1, width) || memcmp(u0, u1, width / 2) || memcmp(v0, v1, width / 2))
                fail();
            bench_new(src1, y1, u1, v1, width);
        }
    }
    report("v210_to_planar10");
}

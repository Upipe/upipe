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

#include "checkasm.h"
#include "lib/upipe-v210/v210dec.h"
#include "lib/upipe-hbrmt/sdienc.h"
#include "lib/upipe-hbrmt/rfc4175_enc.h"

static uint32_t clip(uint32_t value)
{
    if (value < 4)
        return 4;
    if (value > 1019)
        return 1019;
    return value;
}

static uint32_t get_v210(void)
{
    uint32_t t0 = rnd() & 0x3ff,
             t1 = rnd() & 0x3ff,
             t2 = rnd() & 0x3ff;
    uint32_t value =  clip(t0)
                   | (clip(t1) << 10)
                   | (clip(t2) << 20);
    return value;
}

#define NUM_SAMPLES 512

static void randomize_buffers(uint32_t *src0, uint32_t *src1, int len)
{
    for (int i = 0; i < len; i++) {
        uint32_t value = get_v210();
        src0[i] = value;
        src1[i] = value;
    }
}

void checkasm_check_v210_input(void)
{
    struct {
        void (*planar_10)(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);
        void (*planar_8)(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
        void (*sdi)(const uint32_t *src, uint8_t *dst, uintptr_t pixels);
        void (*uyvy)(const uint32_t *src, uint16_t *dst, uintptr_t width);
    } s = {
        .planar_10 = upipe_v210_to_planar_10_c,
        .planar_8  = upipe_v210_to_planar_8_c,
        .sdi       = upipe_v210_to_sdi_c,
        .uyvy      = upipe_v210_to_uyvy_c,
    };

    int cpu_flags = av_get_cpu_flags();

#ifdef HAVE_X86ASM
    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.planar_10 = upipe_v210_to_planar_10_aligned_ssse3;
        s.planar_8  = upipe_v210_to_planar_8_aligned_ssse3;
        s.sdi       = upipe_v210_to_sdi_ssse3;
        s.uyvy      = upipe_v210_to_uyvy_unaligned_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
        s.planar_10 = upipe_v210_to_planar_10_aligned_avx;
        s.planar_8  = upipe_v210_to_planar_8_aligned_avx;
        s.sdi       = upipe_v210_to_sdi_avx;
        s.uyvy      = upipe_v210_to_uyvy_unaligned_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.planar_10 = upipe_v210_to_planar_10_aligned_avx2;
        s.planar_8  = upipe_v210_to_planar_8_aligned_avx2;
        s.sdi       = upipe_v210_to_sdi_avx2;
        s.uyvy      = upipe_v210_to_uyvy_unaligned_avx2;
    }
#endif

    if (check_func(s.planar_8, "v210_to_planar8")) {
        uint32_t src0[NUM_SAMPLES/3];
        uint32_t src1[NUM_SAMPLES/3];
        uint8_t y0[NUM_SAMPLES/2];
        uint8_t y1[NUM_SAMPLES/2];
        uint8_t u0[NUM_SAMPLES/4];
        uint8_t u1[NUM_SAMPLES/4];
        uint8_t v0[NUM_SAMPLES/4];
        uint8_t v1[NUM_SAMPLES/4];
        declare_func(void, const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t width);
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        randomize_buffers(src0, src1, NUM_SAMPLES/3);
        call_ref(src0, y0, u0, v0, pixels);
        call_new(src1, y1, u1, v1, pixels);
        if (memcmp(src0, src1, NUM_SAMPLES/3 * sizeof src0[0])
                || memcmp(y0, y1, pixels)
                || memcmp(u0, u1, pixels/2)
                || memcmp(v0, v1, pixels/2))
            fail();
        bench_new(src1, y1, u1, v1, pixels);
    }
    report("v210_to_planar8");

    if (check_func(s.planar_10, "v210_to_planar10")) {
        uint32_t src0[NUM_SAMPLES/3];
        uint32_t src1[NUM_SAMPLES/3];
        uint16_t y0[NUM_SAMPLES/2];
        uint16_t y1[NUM_SAMPLES/2];
        uint16_t u0[NUM_SAMPLES/4];
        uint16_t u1[NUM_SAMPLES/4];
        uint16_t v0[NUM_SAMPLES/4];
        uint16_t v1[NUM_SAMPLES/4];
        declare_func(void, const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t width);
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        randomize_buffers(src0, src1, NUM_SAMPLES/3);
        call_ref(src0, y0, u0, v0, pixels);
        call_new(src1, y1, u1, v1, pixels);
        if (memcmp(src0, src1, NUM_SAMPLES/3 * sizeof src0[0])
                || memcmp(y0, y1, pixels * sizeof y0[0])
                || memcmp(u0, u1, pixels/2 * sizeof u0[0])
                || memcmp(v0, v1, pixels/2 * sizeof v0[0]))
            fail();
        bench_new(src1, y1, u1, v1, pixels);
    }
    report("v210_to_planar10");

    if (check_func(s.uyvy, "v210_to_uyvy")) {
        uint32_t src0[NUM_SAMPLES / 3];
        uint32_t src1[NUM_SAMPLES / 3];
        uint16_t dst0[NUM_SAMPLES];
        uint16_t dst1[NUM_SAMPLES];
        declare_func(void, const void *src, uint16_t *dst, uintptr_t width);
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        randomize_buffers(src0, src1, NUM_SAMPLES / 3);
        call_ref(src0, dst0, pixels);
        call_new(src1, dst1, pixels);
        if (memcmp(src0, src1, sizeof(src0))
                || memcmp(dst0, dst1, 2*pixels * sizeof dst0[0]))
            fail();
        bench_new(src1, dst1, NUM_SAMPLES / 2);
    }
    report("v210_to_uyvy");

    if (check_func(s.sdi, "v210_to_sdi")) {
        uint32_t src0[NUM_SAMPLES / 3];
        uint32_t src1[NUM_SAMPLES / 3];
        uint8_t dst0[NUM_SAMPLES / 4 * 5];
        uint8_t dst1[NUM_SAMPLES / 4 * 5];
        declare_func(void, const uint32_t *src, uint8_t *dst, uintptr_t pixels);
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        randomize_buffers(src0, src1, NUM_SAMPLES / 3);
        call_ref(src0, dst0, pixels);
        call_new(src1, dst1, pixels);
        if (memcmp(src0, src1, sizeof(src0))
                || memcmp(dst0, dst1, 2*pixels / 3 * sizeof dst0[0]))
            fail();
        bench_new(src1, dst1, pixels);
    }
    report("v210_to_sdi");
}

/*
 * Copyright (c) 2018 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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
#include <libavutil/mem.h>

#include "checkasm.h"
#include "lib/upipe-hbrmt/sdienc.h"
#include "lib/upipe-netmap/sdi.h"
#include "lib/upipe-v210/v210enc.h"

#define NUM_SAMPLES 512

static void randomize_buffers(uint16_t *src0, uint16_t *src1, int len)
{
    for (int i = 0; i < len; i++) {
        src0[i] = src1[i] = rnd() & 0x3ff;
    }
}

void checkasm_check_planar10_input(void)
{
    struct {
        void (*sdi)(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);
        void (*sdi_2)(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
        void (*uyvy)(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels, uint32_t mask);
        void (*v210)(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t pixels);
    } s = {
#ifdef HAVE_NETMAP
        .sdi   = upipe_planar_to_sdi_10_c,
        .sdi_2 = upipe_planar_to_sdi_10_2_c,
#endif
        .uyvy = upipe_planar_to_uyvy_10_c,
        .v210 = upipe_planar_to_v210_10_c,
    };

    int cpu_flags = av_get_cpu_flags();

#ifdef HAVE_X86ASM
    if (cpu_flags & AV_CPU_FLAG_SSE2) {
       s.uyvy = upipe_planar_to_uyvy_10_sse2;
    }
    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
       s.sdi   = upipe_planar_to_sdi_10_ssse3;
       s.sdi_2 = upipe_planar_to_sdi_10_2_ssse3;
       s.v210  = upipe_planar_to_v210_10_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
       s.sdi   = upipe_planar_to_sdi_10_avx;
       s.sdi_2 = upipe_planar_to_sdi_10_2_avx;
       s.uyvy  = upipe_planar_to_uyvy_10_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
       s.sdi   = upipe_planar_to_sdi_10_avx2;
       s.sdi_2 = upipe_planar_to_sdi_10_2_avx2;
       s.uyvy  = upipe_planar_to_uyvy_10_avx2;
       s.v210  = upipe_planar_to_v210_10_avx2;
    }
#endif

    if (check_func(s.sdi, "planar_to_sdi_10")) {
        uint16_t y0[NUM_SAMPLES/2];
        uint16_t y1[NUM_SAMPLES/2];
        uint16_t u0[NUM_SAMPLES/4];
        uint16_t u1[NUM_SAMPLES/4];
        uint16_t v0[NUM_SAMPLES/4];
        uint16_t v1[NUM_SAMPLES/4];
        uint8_t dst0[NUM_SAMPLES * 10 / 8];
        uint8_t dst1[NUM_SAMPLES * 10 / 8];

        declare_func(void, const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);

        randomize_buffers(y0, y1, NUM_SAMPLES/2);
        randomize_buffers(u0, u1, NUM_SAMPLES/4);
        randomize_buffers(v0, v1, NUM_SAMPLES/4);
        call_ref(y0, u0, v0, dst0, NUM_SAMPLES / 2);
        call_new(y1, u1, v1, dst1, NUM_SAMPLES / 2);
        if (memcmp(dst0, dst1, sizeof dst0)
                || memcmp(y0, y1, sizeof y0)
                || memcmp(u0, u1, sizeof u0)
                || memcmp(v0, v1, sizeof v0))
            fail();
        bench_new(y1, u1, v1, dst1, NUM_SAMPLES / 2);
    }
    report("planar_to_sdi_10");

    if (check_func(s.sdi_2, "planar_to_sdi_10_2")) {
        uint16_t y0[NUM_SAMPLES/2];
        uint16_t y1[NUM_SAMPLES/2];
        uint16_t u0[NUM_SAMPLES/4];
        uint16_t u1[NUM_SAMPLES/4];
        uint16_t v0[NUM_SAMPLES/4];
        uint16_t v1[NUM_SAMPLES/4];
        uint8_t dst0[NUM_SAMPLES * 10 / 8 + 31];
        uint8_t dst1[NUM_SAMPLES * 10 / 8 + 31];
        uint8_t dst2[NUM_SAMPLES * 10 / 8 + 31];
        uint8_t dst3[NUM_SAMPLES * 10 / 8 + 31];

        declare_func(void, const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
        randomize_buffers(y0, y1, NUM_SAMPLES/2);
        randomize_buffers(u0, u1, NUM_SAMPLES/4);
        randomize_buffers(v0, v1, NUM_SAMPLES/4);

        call_ref(y0, u0, v0, dst0, dst2, NUM_SAMPLES / 2);
        call_new(y1, u1, v1, dst1, dst3, NUM_SAMPLES / 2);
        if (memcmp(dst0, dst1, NUM_SAMPLES*10/8)
                || memcmp(dst0, dst2, NUM_SAMPLES*10/8)
                || memcmp(dst0, dst3, NUM_SAMPLES*10/8)
                || memcmp(y0, y1, sizeof y0)
                || memcmp(u0, u1, sizeof u0)
                || memcmp(v0, v1, sizeof v0))
            fail();
        bench_new(y1, u1, v1, dst1, dst3, NUM_SAMPLES / 2);
    }
    report("planar_to_sdi_10_2");

    if (check_func(s.uyvy, "planar_to_uyvy_10")) {
        uint16_t y0[NUM_SAMPLES/2];
        uint16_t y1[NUM_SAMPLES/2];
        uint16_t u0[NUM_SAMPLES/4];
        uint16_t u1[NUM_SAMPLES/4];
        uint16_t v0[NUM_SAMPLES/4];
        uint16_t v1[NUM_SAMPLES/4];
        uint16_t dst0[NUM_SAMPLES];
        uint16_t dst1[NUM_SAMPLES];

        declare_func(void, uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels, uint32_t mask);

        randomize_buffers(y0, y1, NUM_SAMPLES/2);
        randomize_buffers(u0, u1, NUM_SAMPLES/4);
        randomize_buffers(v0, v1, NUM_SAMPLES/4);
        call_ref(dst0, y0, u0, v0, NUM_SAMPLES / 2, 0xffff);
        call_new(dst1, y1, u1, v1, NUM_SAMPLES / 2, 0xffff);
        if (memcmp(dst0, dst1, sizeof dst0)
                || memcmp(y0, y1, sizeof y0)
                || memcmp(u0, u1, sizeof u0)
                || memcmp(v0, v1, sizeof v0))
            fail();
        bench_new(dst1, y1, u1, v1, NUM_SAMPLES / 2, 0xffff);
    }
    report("planar_to_uyvy_10");

    if (check_func(s.v210, "planar_to_v210_10")) {
        uint16_t y0[NUM_SAMPLES/2];
        uint16_t y1[NUM_SAMPLES/2];
        uint16_t u0[NUM_SAMPLES/4];
        uint16_t u1[NUM_SAMPLES/4];
        uint16_t v0[NUM_SAMPLES/4];
        uint16_t v1[NUM_SAMPLES/4];
        uint8_t dst0[NUM_SAMPLES * 4 / 3 + 32];
        uint8_t dst1[NUM_SAMPLES * 4 / 3 + 32];
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        declare_func(void, const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t pixels);

        randomize_buffers(y0, y1, NUM_SAMPLES/2);
        randomize_buffers(u0, u1, NUM_SAMPLES/4);
        randomize_buffers(v0, v1, NUM_SAMPLES/4);
        call_ref(y0, u0, v0, dst0, pixels);
        call_new(y1, u1, v1, dst1, pixels);
        if (memcmp(dst0, dst1, pixels * 2 * 4 / 3)
                || memcmp(y0, y1, sizeof y0)
                || memcmp(u0, u1, sizeof u0)
                || memcmp(v0, v1, sizeof v0))
            fail();
        bench_new(y1, u1, v1, dst1, pixels);
    }
    report("planar_to_v210_10");
}

/*
 * Copyright (c) 2017-2018 Open Broadcast Systems Ltd.
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

#define NUM_SAMPLES 512

static void randomize_buffers(uint8_t *src0, uint8_t *src1, int len)
{
    for (int i = 0; i < len; i++) {
        uint8_t byte = rnd();
        src0[i] = byte;
        src1[i] = byte;
    }
}

void checkasm_check_planar8_input(void)
{
    struct {
        void (*uyvy)(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);
    } s = {
        .uyvy = upipe_planar_to_uyvy_8_c,
    };

    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
       s.uyvy =  upipe_planar_to_uyvy_8_unaligned_sse2;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
       s.uyvy =  upipe_planar_to_uyvy_8_unaligned_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
       s.uyvy =  upipe_planar_to_uyvy_8_unaligned_avx2;
    }

    if (check_func(s.uyvy, "planar_to_uyvy_8")) {
        uint8_t y0[NUM_SAMPLES/2];
        uint8_t y1[NUM_SAMPLES/2];
        uint8_t u0[NUM_SAMPLES/4];
        uint8_t u1[NUM_SAMPLES/4];
        uint8_t v0[NUM_SAMPLES/4];
        uint8_t v1[NUM_SAMPLES/4];
        uint16_t dst0[NUM_SAMPLES];
        uint16_t dst1[NUM_SAMPLES];

        declare_func(void, uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);

        randomize_buffers(y0, y1, NUM_SAMPLES/2);
        randomize_buffers(u0, u1, NUM_SAMPLES/4);
        randomize_buffers(v0, v1, NUM_SAMPLES/4);
        call_ref(dst0, y0, u0, v0, NUM_SAMPLES / 2);
        call_new(dst1, y1, u1, v1, NUM_SAMPLES / 2);
        if (memcmp(dst0, dst1, sizeof dst0)
                || memcmp(y0, y1, sizeof y0)
                || memcmp(u0, u1, sizeof u0)
                || memcmp(v0, v1, sizeof v0))
            fail();
        bench_new(dst1, y1, u1, v1, NUM_SAMPLES / 2);
    }
    report("planar_to_uyvy_8");
}

/*
 * Copyright (c) 2017 Open Broadcast Systems Ltd.
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
#include "lib/upipe-hbrmt/sdidec.h"
#include "lib/upipe-hbrmt/rfc4175_dec.h"

#define NUM_SAMPLES 512

static void randomize_buffers(uint16_t *src0, uint16_t *src1)
{
    for (int i = 0; i < NUM_SAMPLES; i++) {
        uint16_t sample = rnd() & 0x3ff;
        src0[i] = sample;
        src1[i] = sample;
    }
}

void checkasm_check_uyvy_input(void)
{
    struct {
        void (*planar10)(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *src, uintptr_t pixels);
        void (*planar8)(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *src, uintptr_t pixels);
        void (*sdi)(uint8_t *dst, const uint8_t *src, uintptr_t pixels);
        void (*v210)(const uint16_t *src, uint8_t *dst, uintptr_t pixels);
    } s = {
        .planar10 = upipe_uyvy_to_planar_10_c,
        .planar8 = upipe_uyvy_to_planar_8_c,
    };

    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.planar10 = upipe_uyvy_to_planar_10_unaligned_ssse3;
        s.planar8 = upipe_uyvy_to_planar_8_unaligned_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
        s.planar10 = upipe_uyvy_to_planar_10_unaligned_avx;
        s.planar8 = upipe_uyvy_to_planar_8_unaligned_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.planar10 = upipe_uyvy_to_planar_10_unaligned_avx2;
        s.planar8 = upipe_uyvy_to_planar_8_unaligned_avx2;
    }

    if (check_func(s.planar10, "uyvy_to_planar10")) {
        uint16_t src0[NUM_SAMPLES];
        uint16_t src1[NUM_SAMPLES];
        uint16_t y0[NUM_SAMPLES/2];
        uint16_t y1[NUM_SAMPLES/2];
        uint16_t u0[NUM_SAMPLES/4];
        uint16_t u1[NUM_SAMPLES/4];
        uint16_t v0[NUM_SAMPLES/4];
        uint16_t v1[NUM_SAMPLES/4];
        declare_func(void, uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *src, uintptr_t pixels);

        randomize_buffers(src0, src1);
        call_ref(y0, u0, v0, src0, NUM_SAMPLES / 2);
        call_new(y1, u1, v1, src1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, NUM_SAMPLES * sizeof src0[0])
                || memcmp(y0, y1, NUM_SAMPLES / 2 * sizeof y0[0])
                || memcmp(u0, u1, NUM_SAMPLES / 4 * sizeof u0[0])
                || memcmp(v0, v1, NUM_SAMPLES / 4 * sizeof v0[0]))
            fail();
        bench_new(y1, u1, v1, src1, NUM_SAMPLES / 2);
    }
    report("uyvy_to_planar10");

    if (check_func(s.planar8, "uyvy_to_planar8")) {
        uint16_t src0[NUM_SAMPLES];
        uint16_t src1[NUM_SAMPLES];
        uint8_t y0[NUM_SAMPLES/2];
        uint8_t y1[NUM_SAMPLES/2];
        uint8_t u0[NUM_SAMPLES/4];
        uint8_t u1[NUM_SAMPLES/4];
        uint8_t v0[NUM_SAMPLES/4];
        uint8_t v1[NUM_SAMPLES/4];
        declare_func(void, uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *src, uintptr_t pixels);

        randomize_buffers(src0, src1);
        call_ref(y0, u0, v0, src0, NUM_SAMPLES / 2);
        call_new(y1, u1, v1, src1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, NUM_SAMPLES * sizeof src0[0])
                || memcmp(y0, y1, NUM_SAMPLES / 2 * sizeof y0[0])
                || memcmp(u0, u1, NUM_SAMPLES / 4 * sizeof u0[0])
                || memcmp(v0, v1, NUM_SAMPLES / 4 * sizeof v0[0]))
            fail();
        bench_new(y1, u1, v1, src1, NUM_SAMPLES / 2);
    }
    report("uyvy_to_planar8");
}

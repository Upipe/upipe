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

static void randomize_buffers(uint8_t *src0, uint8_t *src1)
{
    for (int i = 0; i < NUM_SAMPLES * 10 / 8; i++) {
        uint8_t byte = rnd();
        src0[i] = byte;
        src1[i] = byte;
    }
}

void checkasm_check_sdidec(void)
{
    struct {
        void (*planar10)(const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);
        void (*planar8)(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
        void (*uyvy)(const uint8_t *src, uint16_t *dst, uintptr_t pixels);
        void (*v210)(const uint8_t *src, uint32_t *dst, uintptr_t pixels);
    } s = {
        .planar10 = upipe_sdi_to_planar_10_c,
        .planar8 = upipe_sdi_to_planar_8_c,
        .uyvy = upipe_sdi_to_uyvy_c,
        .v210 = upipe_sdi_to_v210_c,
    };

    int cpu_flags = av_get_cpu_flags();

#ifdef HAVE_X86ASM
    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.planar10 = upipe_sdi_to_planar_10_ssse3;
        s.planar8 = upipe_sdi_to_planar_8_ssse3;
        s.uyvy = upipe_sdi_to_uyvy_aligned_ssse3;
        s.v210 = upipe_sdi_to_v210_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
        s.planar10 = upipe_sdi_to_planar_10_avx;
        s.planar8 = upipe_sdi_to_planar_8_avx;
        s.v210 = upipe_sdi_to_v210_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.planar10 = upipe_sdi_to_planar_10_avx2;
        s.planar8 = upipe_sdi_to_planar_8_avx2;
        s.uyvy = upipe_sdi_to_uyvy_aligned_avx2;
        s.v210 = upipe_sdi_to_v210_avx2;
    }
#endif

    if (check_func(s.planar10, "sdi_to_planar10")) {
        uint8_t src0[NUM_SAMPLES * 10 / 8];
        uint8_t src1[NUM_SAMPLES * 10 / 8];
        uint16_t y0[NUM_SAMPLES/2 + 16];
        uint16_t y1[NUM_SAMPLES/2 + 16];
        uint16_t u0[NUM_SAMPLES/4 + 16];
        uint16_t u1[NUM_SAMPLES/4 + 16];
        uint16_t v0[NUM_SAMPLES/4 + 16];
        uint16_t v1[NUM_SAMPLES/4 + 16];

        declare_func(void, const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t size);

        randomize_buffers(src0, src1);
        call_ref(src0, y0, u0, v0, NUM_SAMPLES / 2);
        call_new(src1, y1, u1, v1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, NUM_SAMPLES * 10 / 8)
                || memcmp(y0, y1, NUM_SAMPLES / 2 * sizeof(y0[0]))
                || memcmp(u0, u1, NUM_SAMPLES / 4 * sizeof(u0[0]))
                || memcmp(v0, v1, NUM_SAMPLES / 4 * sizeof(v0[0])))
            fail();
        bench_new(src1, y1, u1, v1, NUM_SAMPLES / 2);
    }
    report("sdi_to_planar10");

    if (check_func(s.planar8, "sdi_to_planar8")) {
        uint8_t src0[NUM_SAMPLES * 10 / 8];
        uint8_t src1[NUM_SAMPLES * 10 / 8];
        uint8_t y0[NUM_SAMPLES/2 + 32];
        uint8_t y1[NUM_SAMPLES/2 + 32];
        uint8_t u0[NUM_SAMPLES/4 + 32];
        uint8_t u1[NUM_SAMPLES/4 + 32];
        uint8_t v0[NUM_SAMPLES/4 + 32];
        uint8_t v1[NUM_SAMPLES/4 + 32];

        declare_func(void, const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);

        randomize_buffers(src0, src1);
        call_ref(src0, y0, u0, v0, NUM_SAMPLES / 2);
        call_new(src1, y1, u1, v1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, NUM_SAMPLES * 10 / 8)
                || memcmp(y0, y1, NUM_SAMPLES / 2)
                || memcmp(u0, u1, NUM_SAMPLES / 4)
                || memcmp(v0, v1, NUM_SAMPLES / 4))
            fail();
        bench_new(src1, y1, u1, v1, NUM_SAMPLES / 2);
    }
    report("sdi_to_planar8");

    if (check_func(s.uyvy, "sdi_to_uyvy")) {
        uint8_t  src0[NUM_SAMPLES * 10 / 8];
        uint8_t  src1[NUM_SAMPLES * 10 / 8];
        DECLARE_ALIGNED(32, uint16_t, dst0)[NUM_SAMPLES];
        DECLARE_ALIGNED(32, uint16_t, dst1)[NUM_SAMPLES];
        declare_func(void, const uint8_t *src, uint16_t *dst, uintptr_t pixels);

        randomize_buffers(src0, src1);
        call_ref(src0, dst0, NUM_SAMPLES / 2);
        call_new(src1, dst1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, NUM_SAMPLES * 10 / 8)
                || memcmp(dst0, dst1, NUM_SAMPLES))
            fail();
        bench_new(src1, dst1, NUM_SAMPLES / 2);
    }
    report("sdi_to_uyvy");

    if (check_func(s.v210, "sdi_to_v210")) {
        uint8_t src0[NUM_SAMPLES * 10 / 8];
        uint8_t src1[NUM_SAMPLES * 10 / 8];
        DECLARE_ALIGNED(32, uint32_t, dst0)[NUM_SAMPLES / 3 + 8];
        DECLARE_ALIGNED(32, uint32_t, dst1)[NUM_SAMPLES / 3 + 8];
        declare_func(void, const uint8_t *src, uint32_t *dst, uintptr_t pixels);
        const int pixels = NUM_SAMPLES / 2 / 6 * 6;

        randomize_buffers(src0, src1);
        call_ref(src0, dst0, pixels);
        call_new(src1, dst1, pixels);
        if (memcmp(src0, src1, NUM_SAMPLES * 10 / 8)
                || memcmp(dst0, dst1, 2*pixels / 3 * sizeof dst0[0]))
            fail();
        bench_new(src1, dst1, pixels);
    }
    report("sdi_to_v210");
}

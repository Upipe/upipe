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
#include "lib/upipe-pciesdi/levelb.h"

#define NUM_SAMPLES 512

static void randomize_buffers_unpacked(uint16_t *src0, uint16_t *src1)
{
    for (int i = 0; i < NUM_SAMPLES; i++) {
        src0[i] = src1[i] = rnd() & 0x3ff;
    }
}

static void randomize_buffers_packed(uint8_t *src0, uint8_t *src1)
{
    for (int i = 0; i < 2 * NUM_SAMPLES * 10 / 8; i++) {
        src0[i] = src1[i] = rnd();
    }
}

void checkasm_check_sdi3g_input(void)
{
    struct {
        void (*packed)(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
    } s = {
        .packed = upipe_sdi3g_to_uyvy_2_c,
    };

    int cpu_flags = av_get_cpu_flags();

#ifdef HAVE_X86ASM
    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.packed = upipe_sdi3g_to_uyvy_2_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
        s.packed = upipe_sdi3g_to_uyvy_2_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.packed = upipe_sdi3g_to_uyvy_2_avx2;
    }
#endif

    if (check_func(s.packed, "packed")) {
        uint8_t  src0[2 * NUM_SAMPLES * 10 / 8];
        uint8_t  src1[2 * NUM_SAMPLES * 10 / 8];
        uint16_t dst0[NUM_SAMPLES];
        uint16_t dst1[NUM_SAMPLES];
        uint16_t dst2[NUM_SAMPLES];
        uint16_t dst3[NUM_SAMPLES];
        declare_func(void, const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);

        randomize_buffers_packed(src0, src1);
        call_ref(src0, dst0, dst2, NUM_SAMPLES / 2);
        call_new(src1, dst1, dst3, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, sizeof src0)
                || memcmp(dst0, dst1, sizeof dst0)
                || memcmp(dst2, dst3, sizeof dst2))
            fail();
        bench_new(src1, dst1, dst3, NUM_SAMPLES / 2);
    }
    report("packed");
}

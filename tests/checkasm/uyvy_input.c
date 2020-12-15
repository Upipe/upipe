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

#include "checkasm.h"
#include "lib/upipe-hbrmt/sdidec.h"
#include "lib/upipe-hbrmt/sdienc.h"

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
        void (*sdi)(uint8_t *dst, const uint8_t *src, uintptr_t pixels);
    } s = {
#ifdef HAVE_BITSTREAM_COMMON_H
        .sdi = upipe_uyvy_to_sdi_c,
#endif
    };

#ifdef HAVE_X86ASM
#ifdef HAVE_BITSTREAM_COMMON_H
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.sdi = upipe_uyvy_to_sdi_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX) {
        s.sdi = upipe_uyvy_to_sdi_avx;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.sdi = upipe_uyvy_to_sdi_avx2;
    }
#endif
#endif

    if (check_func(s.sdi, "uyvy_to_sdi")) {
        uint16_t src0[NUM_SAMPLES];
        uint16_t src1[NUM_SAMPLES];
        uint8_t dst0[NUM_SAMPLES * 10 / 8 + 31];
        uint8_t dst1[NUM_SAMPLES * 10 / 8 + 31];
        declare_func(void, uint8_t *dst, const uint8_t *src, uintptr_t samples);

        randomize_buffers(src0, src1);
        call_ref(dst0, (const uint8_t*)src0, NUM_SAMPLES / 2);
        call_new(dst1, (const uint8_t*)src1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, sizeof src0)
                || memcmp(dst0, dst1, NUM_SAMPLES * 10 / 8))
            fail();
        bench_new(dst1, (const uint8_t*)src1, NUM_SAMPLES / 2);
    }
    report("uyvy_to_sdi");
}

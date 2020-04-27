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
        void (*uyvy)(const uint8_t *src, uint16_t *dst, int64_t pixels);
    } s = {
        .uyvy = upipe_sdi_to_uyvy_c,
    };

#ifdef HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        s.uyvy = upipe_sdi_to_uyvy_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.uyvy = upipe_sdi_to_uyvy_avx2;
    }
#endif

    if (check_func(s.uyvy, "sdi_to_uyvy")) {
        uint8_t  src0[NUM_SAMPLES * 10 / 8];
        uint8_t  src1[NUM_SAMPLES * 10 / 8];
        DECLARE_ALIGNED(32, uint16_t, dst0)[NUM_SAMPLES];
        DECLARE_ALIGNED(32, uint16_t, dst1)[NUM_SAMPLES];
        declare_func(void, const uint8_t *src, uint16_t *dst, int64_t pixels);

        randomize_buffers(src0, src1);
        call_ref(src0, dst0, NUM_SAMPLES / 2);
        call_new(src1, dst1, NUM_SAMPLES / 2);
        if (memcmp(src0, src1, NUM_SAMPLES * 10 / 8)
                || memcmp(dst0, dst1, NUM_SAMPLES))
            fail();
        bench_new(src1, dst1, NUM_SAMPLES / 2);
    }
    report("sdi_to_uyvy");
}

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
#include "lib/upipe-hbrmt/sdidec.h"
#include "lib/upipe-hbrmt/rfc4175_dec.h"

#define NUM_SAMPLES 512

static void randomize_buffers(uint16_t *src0, uint16_t *src1)
{
    for (int i = 0; i < NUM_SAMPLES; i++) {
        src0[i] = src1[i] = rnd() & 0x3ff;
    }
}

void upipe_sdi3g_levelb_unpack_sse2(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
static void levelb_unpack(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i++) {
        dst1[2*i + 0] = src[4*i + 0];
        dst1[2*i + 1] = src[4*i + 1];
        dst2[2*i + 0] = src[4*i + 2];
        dst2[2*i + 1] = src[4*i + 3];
    }
}

void checkasm_check_sdi3g_input(void)
{
    struct {
        void (*uyvy)(const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
    } s = {
        .uyvy = levelb_unpack,
    };

    int cpu_flags = av_get_cpu_flags();

#ifdef HAVE_X86ASM
    if (cpu_flags & AV_CPU_FLAG_SSE2) {
        s.uyvy = upipe_sdi3g_levelb_unpack_sse2;
    }
#endif

    if (check_func(s.uyvy, "sdi3g_levelb_unpack")) {
        uint16_t src0[NUM_SAMPLES];
        uint16_t src1[NUM_SAMPLES];
        uint16_t dst0[NUM_SAMPLES / 2];
        uint16_t dst1[NUM_SAMPLES / 2];
        uint16_t dst2[NUM_SAMPLES / 2];
        uint16_t dst3[NUM_SAMPLES / 2];
        declare_func(void, const uint16_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);

        randomize_buffers(src0, src1);
        call_ref(src0, dst0, dst2, NUM_SAMPLES / 4);
        call_new(src1, dst1, dst3, NUM_SAMPLES / 4);
        if (memcmp(src0, src1, sizeof src0)
                || memcmp(dst0, dst1, sizeof dst0)
                || memcmp(dst2, dst3, sizeof dst2))
            fail();
        bench_new(src1, dst1, dst3, NUM_SAMPLES / 4);
    }
    report("sdi3g_levelb_unpack");
}

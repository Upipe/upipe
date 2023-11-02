/*
 * Copyright (c) 2022-2023 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley, Kieran Kunhya
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

#include <stdbool.h>
#include <stdint.h>

#include "checkasm.h"
#include "lib/upipe-hbrmt/upipe_hbrmt_common.h"

void upipe_compute_sdi_crc_avx2(uint32_t *crcc, uint32_t *crcy, const uint16_t *uyvy, uintptr_t pixels);

#define NUM_SAMPLES (1280*2)

static uint32_t sdi_crc_lut[8][1024];

static void randomize_buffers(uint16_t *src0, int len)
{
    for (int i = 0; i < len; i++) {
        src0[i] = rnd() & 0x3ff;
    }
}

static uint32_t sdi_crc(uint32_t crc, const uint16_t *data, uintptr_t data_len)
{
    for(int i = 0; i < data_len; i++) {
        crc ^= data[2*i] & 0x3ff;
        for (int k = 0; k < 10; k++)
            crc = crc & 1 ? (crc >> 1) ^ 0x23000 : crc >> 1;
    }
    return crc;
}

static void sdi_crc_unoptimized(uint32_t *crc_c, uint32_t *crc_y, const uint16_t *src, uintptr_t pixels)
{
    *crc_c = sdi_crc(*crc_c, src, pixels);
    *crc_y = sdi_crc(*crc_y, src+1, pixels);
}

static void sdi_crc_optimized1(uint32_t *crc_c, uint32_t *crc_y, const uint16_t *src, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i++) {
        sdi_crc_update(sdi_crc_lut[0], crc_c, src[2*i+0]);
        sdi_crc_update(sdi_crc_lut[0], crc_y, src[2*i+1]);
    }
}

static void sdi_crc_optimized2(uint32_t *crc_c, uint32_t *crc_y, const uint16_t *src, uintptr_t pixels)
{
    for (int i = 0; i < pixels*2; i += 16) {
        sdi_crc_update_blk(sdi_crc_lut, crc_c, crc_y, &src[i]);
    }
}

static void assembly_wrap(uint32_t *crc_c, uint32_t *crc_y, const uint16_t *src, uintptr_t pixels)
{
    uintptr_t remainder = pixels % 12;
    upipe_compute_sdi_crc_avx2(crc_c, crc_y, src, pixels-remainder);
    if (remainder) {
        src += 2*(pixels-remainder);
        for (uintptr_t i = 0; i < remainder; i++) {
            sdi_crc_update(sdi_crc_lut[0], crc_c, src[2*i+0]);
            sdi_crc_update(sdi_crc_lut[0], crc_y, src[2*i+1]);
        }
    }
}

void checkasm_check_sdi_crc(void)
{
    struct {
        /* final proposed function */
        void (*final)(uint32_t *crc_c, uint32_t *crc_y, const uint16_t *src, uintptr_t pixels);
    } s = {
        .final = sdi_crc_unoptimized,
    };

    static bool inited = false;
    if (!inited) {
        sdi_crc_setup(sdi_crc_lut);
        inited = true;
    }

    int cpu_flags = av_get_cpu_flags();

#ifdef HAVE_X86ASM
    if (cpu_flags & AV_CPU_FLAG_MMX) {
        s.final = sdi_crc_optimized1;
    }

    if (cpu_flags & AV_CPU_FLAG_MMXEXT) {
        s.final = sdi_crc_optimized2;
    }

    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        s.final = assembly_wrap;
    }
#endif

    if (check_func(s.final, "final")) {
        uint16_t src0[NUM_SAMPLES];
        uint32_t crc_c_ref = 0, crc_c = 0;
        uint32_t crc_y_ref = 0, crc_y = 0;

        declare_func(void, uint32_t *, uint32_t *, const uint16_t *, uintptr_t);

        randomize_buffers(src0, NUM_SAMPLES);
        call_ref(&crc_c_ref, &crc_y_ref, src0, NUM_SAMPLES/2);
        call_new(&crc_c, &crc_y, src0, NUM_SAMPLES/2);

        if (crc_c_ref != crc_c || crc_y_ref != crc_y) {
            printf("crc_c_ref %x crc_c %x crc_y_ref %x crc_y %x \n", crc_c_ref, crc_c, crc_y_ref, crc_y);
            fail();
        }
        bench_new(&crc_c, &crc_y, src0, NUM_SAMPLES/2);
    }
    report("final");
}

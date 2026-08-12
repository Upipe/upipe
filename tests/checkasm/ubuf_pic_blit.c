/*
 * Copyright (c) 2026 Open Broadcast Systems Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "checkasm.h"

#ifdef HAVE_X86ASM
#include "lib/upipe/x86/ubuf_pic_blit.h"
#endif

/* Not a multiple of any vector width, so the scalar tail runs too, and at least
 * mmsize/2 samples for the widest kernel, which the dispatcher guarantees by
 * declining anything under 16. */
#define NUM_SAMPLES 517
/* Samples past the end are left alone by the reference, so they catch a kernel
 * writing beyond the line. */
#define GUARD 64

static void randomize_buffers(uint16_t *bg, uint16_t *src, uint16_t *alpha,
                              size_t hsize, uint8_t hsub)
{
    for (size_t i = 0; i < hsize; i++) {
        bg[i]  = rnd() & 0x3ff;
        src[i] = rnd() & 0x3ff;
    }
    for (size_t i = 0; i < hsize * hsub; i++)
        alpha[i] = rnd() & 0x3ff;
}

/* The blending the kernels replace, as ubuf_pic_blit_alpha10_bg() spells it for
 * a saturated threshold and a saturated alpha multiplier. */
#define BLEND10_REF(name, hsub)                                             \
static void name(uint16_t *dst, const uint16_t *bg, const uint16_t *src,    \
                 const uint16_t *alpha, size_t hsize)                       \
{                                                                           \
    for (size_t j = 0; j < hsize; j++) {                                    \
        const uint16_t a = alpha[j * (hsub)];                               \
        dst[j] = (bg[j] * (0x3ff - a) + src[j] * a) / 0x3ff;                \
    }                                                                       \
}

BLEND10_REF(blit10_blend_hsub1_ref, 1)
BLEND10_REF(blit10_blend_hsub2_ref, 2)

static void check_blend10(void (*func)(uint16_t *, const uint16_t *,
                                       const uint16_t *, const uint16_t *,
                                       size_t),
                          const char *name, uint8_t hsub)
{
    if (!check_func(func, "%s", name))
        return;

    uint16_t bg[NUM_SAMPLES];
    uint16_t src[NUM_SAMPLES];
    uint16_t alpha[NUM_SAMPLES * 2];
    uint16_t dst0[NUM_SAMPLES + GUARD];
    uint16_t dst1[NUM_SAMPLES + GUARD];

    declare_func(void, uint16_t *dst, const uint16_t *bg, const uint16_t *src,
                 const uint16_t *alpha, size_t hsize);

    randomize_buffers(bg, src, alpha, NUM_SAMPLES, hsub);
    /* A value the blend cannot produce, so a sample left unwritten is caught
     * rather than matching by luck. */
    memset(dst0, 0xa5, sizeof dst0);
    memset(dst1, 0xa5, sizeof dst1);

    call_ref(dst0, bg, src, alpha, NUM_SAMPLES);
    call_new(dst1, bg, src, alpha, NUM_SAMPLES);
    if (memcmp(dst0, dst1, sizeof dst0))
        fail();

    bench_new(dst1, bg, src, alpha, NUM_SAMPLES);
}

void checkasm_check_ubuf_pic_blit(void)
{
    void (*blend_hsub1)(uint16_t *, const uint16_t *, const uint16_t *,
                        const uint16_t *, size_t) = blit10_blend_hsub1_ref;
    void (*blend_hsub2)(uint16_t *, const uint16_t *, const uint16_t *,
                        const uint16_t *, size_t) = blit10_blend_hsub2_ref;

#ifdef HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_SSSE3) {
        blend_hsub1 = upipe_blit10_blend_hsub1_ssse3;
        blend_hsub2 = upipe_blit10_blend_hsub2_ssse3;
    }
    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        blend_hsub1 = upipe_blit10_blend_hsub1_avx2;
        blend_hsub2 = upipe_blit10_blend_hsub2_avx2;
    }
#endif

    check_blend10(blend_hsub1, "blit10_blend_hsub1", 1);
    report("blit10_blend_hsub1");

    check_blend10(blend_hsub2, "blit10_blend_hsub2", 2);
    report("blit10_blend_hsub2");
}

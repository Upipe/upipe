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

/* The 8 bit blending, as ubuf_pic_blit_alpha8_bg() spells it for a saturated
 * threshold and a saturated alpha multiplier. */
#define BLEND8_REF(name, hsub)                                              \
static void name(uint8_t *dst, const uint8_t *bg, const uint8_t *src,       \
                 const uint8_t *alpha, size_t hsize)                        \
{                                                                           \
    for (size_t j = 0; j < hsize; j++) {                                    \
        const uint8_t a = alpha[j * (hsub)];                                \
        dst[j] = (bg[j] * (0xff - a) + src[j] * a) / 0xff;                  \
    }                                                                       \
}

BLEND8_REF(blit8_blend_hsub1_ref, 1)
BLEND8_REF(blit8_blend_hsub2_ref, 2)

static void check_blend8(void (*func)(uint8_t *, const uint8_t *,
                                      const uint8_t *, const uint8_t *,
                                      size_t),
                         const char *name, uint8_t hsub)
{
    if (!check_func(func, "%s", name))
        return;

    uint8_t bg[NUM_SAMPLES];
    uint8_t src[NUM_SAMPLES];
    uint8_t alpha[NUM_SAMPLES * 2];
    uint8_t dst0[NUM_SAMPLES + GUARD];
    uint8_t dst1[NUM_SAMPLES + GUARD];

    declare_func(void, uint8_t *dst, const uint8_t *bg, const uint8_t *src,
                 const uint8_t *alpha, size_t hsize);

    for (size_t i = 0; i < NUM_SAMPLES; i++) {
        bg[i]  = rnd();
        src[i] = rnd();
    }
    /* the extremes of alpha and of the samples too */
    for (size_t i = 0; i < NUM_SAMPLES * hsub; i++)
        alpha[i] = i < 8 ? (i & 1 ? 0xff : 0) : rnd();
    bg[0] = src[1] = 0;
    bg[1] = src[0] = 0xff;
    memset(dst0, 0xa5, sizeof dst0);
    memset(dst1, 0xa5, sizeof dst1);

    call_ref(dst0, bg, src, alpha, NUM_SAMPLES);
    call_new(dst1, bg, src, alpha, NUM_SAMPLES);
    if (memcmp(dst0, dst1, sizeof dst0))
        fail();

    bench_new(dst1, bg, src, alpha, NUM_SAMPLES);
}

/* The selection the threshold kernels replace. */
#define THRESHOLD_REF(name, type, hsub)                                     \
static void name(type *dst, const type *bg, const type *src,                \
                 const type *alpha, size_t hsize, int threshold)            \
{                                                                           \
    for (size_t j = 0; j < hsize; j++)                                      \
        dst[j] = alpha[j * (hsub)] > threshold ? src[j] : bg[j];            \
}

THRESHOLD_REF(blit8_threshold_hsub1_ref, uint8_t, 1)
THRESHOLD_REF(blit8_threshold_hsub2_ref, uint8_t, 2)
THRESHOLD_REF(blit10_threshold_hsub1_ref, uint16_t, 1)
THRESHOLD_REF(blit10_threshold_hsub2_ref, uint16_t, 2)

/* A multiple of every vector width, the short tail above, a tail long enough
 * to need both alpha vectors of a subsampled kernel, and the smallest width
 * the dispatcher passes on. */
#define THRESHOLD_MAX 562

#define CHECK_THRESHOLD(bits, type, max, min_hsize)                         \
static void check_threshold##bits(void (*func)(type *, const type *,        \
                                               const type *, const type *,  \
                                               size_t, int),                \
                                  const char *name)                         \
{                                                                           \
    static const size_t widths[] = { 512, NUM_SAMPLES, THRESHOLD_MAX,       \
                                     min_hsize };                           \
                                                                            \
    if (!check_func(func, "%s", name))                                      \
        return;                                                             \
                                                                            \
    type bg[THRESHOLD_MAX];                                                 \
    type src[THRESHOLD_MAX];                                                \
    type alpha[THRESHOLD_MAX * 2];                                          \
    type dst0[THRESHOLD_MAX + GUARD];                                       \
    type dst1[THRESHOLD_MAX + GUARD];                                       \
                                                                            \
    declare_func(void, type *dst, const type *bg, const type *src,          \
                 const type *alpha, size_t hsize, int threshold);           \
                                                                            \
    for (size_t i = 0; i < THRESHOLD_MAX; i++) {                            \
        bg[i]  = rnd() & (max);                                             \
        src[i] = rnd() & (max);                                             \
    }                                                                       \
    for (size_t i = 0; i < THRESHOLD_MAX * 2; i++)                          \
        alpha[i] = rnd() & (max);                                           \
    /* neither 0 nor the maximum, which the dispatcher sends elsewhere */   \
    int threshold = 1 + rnd() % ((max) - 1);                                \
                                                                            \
    for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {       \
        memset(dst0, 0xa5, sizeof dst0);                                    \
        memset(dst1, 0xa5, sizeof dst1);                                    \
        call_ref(dst0, bg, src, alpha, widths[w], threshold);               \
        call_new(dst1, bg, src, alpha, widths[w], threshold);               \
        if (memcmp(dst0, dst1, sizeof dst0))                                \
            fail();                                                         \
    }                                                                       \
                                                                            \
    bench_new(dst1, bg, src, alpha, NUM_SAMPLES, threshold);                \
}

CHECK_THRESHOLD(8, uint8_t, 0xff, 32)
CHECK_THRESHOLD(10, uint16_t, 0x3ff, 16)

void checkasm_check_ubuf_pic_blit(void)
{
    void (*threshold8_hsub1)(uint8_t *, const uint8_t *, const uint8_t *,
                             const uint8_t *, size_t, int) =
        blit8_threshold_hsub1_ref;
    void (*threshold8_hsub2)(uint8_t *, const uint8_t *, const uint8_t *,
                             const uint8_t *, size_t, int) =
        blit8_threshold_hsub2_ref;
    void (*threshold10_hsub1)(uint16_t *, const uint16_t *, const uint16_t *,
                              const uint16_t *, size_t, int) =
        blit10_threshold_hsub1_ref;
    void (*threshold10_hsub2)(uint16_t *, const uint16_t *, const uint16_t *,
                              const uint16_t *, size_t, int) =
        blit10_threshold_hsub2_ref;
    void (*blend_hsub1)(uint16_t *, const uint16_t *, const uint16_t *,
                        const uint16_t *, size_t) = blit10_blend_hsub1_ref;
    void (*blend_hsub2)(uint16_t *, const uint16_t *, const uint16_t *,
                        const uint16_t *, size_t) = blit10_blend_hsub2_ref;
    void (*blend8_hsub1)(uint8_t *, const uint8_t *, const uint8_t *,
                         const uint8_t *, size_t) = blit8_blend_hsub1_ref;
    void (*blend8_hsub2)(uint8_t *, const uint8_t *, const uint8_t *,
                         const uint8_t *, size_t) = blit8_blend_hsub2_ref;

#ifdef HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (cpu_flags & AV_CPU_FLAG_AVX2) {
        blend_hsub1 = upipe_blit10_blend_hsub1_avx2;
        blend_hsub2 = upipe_blit10_blend_hsub2_avx2;
        blend8_hsub1 = upipe_blit8_blend_hsub1_avx2;
        blend8_hsub2 = upipe_blit8_blend_hsub2_avx2;
        threshold8_hsub1 = upipe_blit8_threshold_hsub1_avx2;
        threshold8_hsub2 = upipe_blit8_threshold_hsub2_avx2;
        threshold10_hsub1 = upipe_blit10_threshold_hsub1_avx2;
        threshold10_hsub2 = upipe_blit10_threshold_hsub2_avx2;
    }
#endif

    check_blend10(blend_hsub1, "blit10_blend_hsub1", 1);
    report("blit10_blend_hsub1");

    check_blend10(blend_hsub2, "blit10_blend_hsub2", 2);
    report("blit10_blend_hsub2");

    check_blend8(blend8_hsub1, "blit8_blend_hsub1", 1);
    report("blit8_blend_hsub1");

    check_blend8(blend8_hsub2, "blit8_blend_hsub2", 2);
    report("blit8_blend_hsub2");

    check_threshold8(threshold8_hsub1, "blit8_threshold_hsub1");
    report("blit8_threshold_hsub1");

    check_threshold8(threshold8_hsub2, "blit8_threshold_hsub2");
    report("blit8_threshold_hsub2");

    check_threshold10(threshold10_hsub1, "blit10_threshold_hsub1");
    report("blit10_threshold_hsub1");

    check_threshold10(threshold10_hsub2, "blit10_threshold_hsub2");
    report("blit10_threshold_hsub2");
}

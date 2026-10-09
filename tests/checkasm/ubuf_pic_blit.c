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
#include <stdio.h>
#include <stdlib.h>
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

/* Neither 0 nor the maximum, which the dispatcher sends elsewhere: the
 * extremes, a random one in each half and the two around the signed byte
 * boundary. */
#define THRESHOLDS(max) {                                                   \
    1, 1 + rnd() % ((max) / 2), 127, 128,                                   \
    (max) / 2 + 1 + rnd() % ((max) / 2 - 1), (max) - 1 }

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
    const int thresholds[] = THRESHOLDS(max);                               \
                                                                            \
    for (size_t t = 0; t < sizeof(thresholds) / sizeof(thresholds[0]); t++) \
        for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {   \
            memset(dst0, 0xa5, sizeof dst0);                                \
            memset(dst1, 0xa5, sizeof dst1);                                \
            call_ref(dst0, bg, src, alpha, widths[w], thresholds[t]);       \
            call_new(dst1, bg, src, alpha, widths[w], thresholds[t]);       \
            if (memcmp(dst0, dst1, sizeof dst0))                            \
                fail();                                                     \
        }                                                                   \
                                                                            \
    bench_new(dst1, bg, src, alpha, NUM_SAMPLES, thresholds[1]);            \
}

CHECK_THRESHOLD(8, uint8_t, 0xff, 32)
CHECK_THRESHOLD(10, uint16_t, 0x3ff, 16)

/* In place, the background being the destination. */
#define INPLACE_REF(name, type, hsub)                                       \
static void name(type *dst, const type *src, const type *alpha,             \
                 size_t hsize, int threshold)                               \
{                                                                           \
    for (size_t j = 0; j < hsize; j++)                                      \
        if (alpha[j * (hsub)] > threshold)                                  \
            dst[j] = src[j];                                                \
}

INPLACE_REF(blit8_inplace_hsub1_ref, uint8_t, 1)
INPLACE_REF(blit8_inplace_hsub2_ref, uint8_t, 2)
INPLACE_REF(blit10_inplace_hsub1_ref, uint16_t, 1)
INPLACE_REF(blit10_inplace_hsub2_ref, uint16_t, 2)

#define CHECK_INPLACE(bits, type, max, min_hsize)                           \
static void check_inplace##bits(void (*func)(type *, const type *,          \
                                             const type *, size_t, int),    \
                                const char *name)                           \
{                                                                           \
    static const size_t widths[] = { 512, NUM_SAMPLES, THRESHOLD_MAX,       \
                                     min_hsize };                           \
                                                                            \
    if (!check_func(func, "%s", name))                                      \
        return;                                                             \
                                                                            \
    type bg[THRESHOLD_MAX + GUARD];                                         \
    type src[THRESHOLD_MAX];                                                \
    type alpha[THRESHOLD_MAX * 2];                                          \
    type dst0[THRESHOLD_MAX + GUARD];                                       \
    type dst1[THRESHOLD_MAX + GUARD];                                       \
                                                                            \
    declare_func(void, type *dst, const type *src, const type *alpha,       \
                 size_t hsize, int threshold);                              \
                                                                            \
    memset(bg, 0xa5, sizeof bg);                                            \
    for (size_t i = 0; i < THRESHOLD_MAX; i++) {                            \
        bg[i]  = rnd() & (max);                                             \
        src[i] = rnd() & (max);                                             \
    }                                                                       \
    for (size_t i = 0; i < THRESHOLD_MAX * 2; i++)                          \
        alpha[i] = rnd() & (max);                                           \
    const int thresholds[] = THRESHOLDS(max);                               \
                                                                            \
    for (size_t t = 0; t < sizeof(thresholds) / sizeof(thresholds[0]); t++) \
        for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {   \
            memcpy(dst0, bg, sizeof dst0);                                  \
            memcpy(dst1, bg, sizeof dst1);                                  \
            call_ref(dst0, src, alpha, widths[w], thresholds[t]);           \
            call_new(dst1, src, alpha, widths[w], thresholds[t]);           \
            if (memcmp(dst0, dst1, sizeof dst0))                            \
                fail();                                                     \
        }                                                                   \
                                                                            \
    bench_new(dst1, src, alpha, NUM_SAMPLES, thresholds[1]);                \
}

CHECK_INPLACE(8, uint8_t, 0xff, 32)
CHECK_INPLACE(10, uint16_t, 0x3ff, 16)

/* A subtitle strip a frame wide, whose planes overflow the L2 cache, and
 * alpha as text gives it: half of the 64 sample blocks transparent, the
 * others runs of transparent and opaque samples.  The buffers are 64 byte
 * aligned, as the picture planes are.  Checked at a threshold on each side
 * of the middle, timed at the low one; C is checked but not timed. */
#define FRAME_SAMPLES (1920 * 256)
#define FRAME_THRESHOLD(max) ((max) / 13)

#define TEXT_ALPHA(bits, type, max)                                         \
static void text_alpha##bits(type *alpha, size_t n)                         \
{                                                                           \
    for (size_t b = 0; b < n; b += 64) {                                    \
        size_t end = b + 64 < n ? b + 64 : n;                               \
        if (rnd() & 1) {                                                    \
            for (size_t i = b; i < end; i++)                                \
                alpha[i] = 0;                                               \
            continue;                                                       \
        }                                                                   \
        type a = 0;                                                         \
        for (size_t i = b; i < end; ) {                                     \
            size_t run = 2 + rnd() % 11;                                    \
            for (; run && i < end; run--, i++)                              \
                alpha[i] = a;                                               \
            a = a ? 0 : (max);                                              \
        }                                                                   \
    }                                                                       \
}

TEXT_ALPHA(8, uint8_t, 0xff)
TEXT_ALPHA(10, uint16_t, 0x3ff)

#define CHECK_FRAME(bits, type, max)                                        \
static void check_frame##bits(void (*func)(type *, const type *,            \
                                           const type *, const type *,      \
                                           size_t, int),                    \
                              void (*ref)(type *, const type *,             \
                                          const type *, const type *,       \
                                          size_t, int),                     \
                              const char *name, uint8_t hsub)               \
{                                                                           \
    if (!check_func(func, "%s_frame", name))                                \
        return;                                                             \
                                                                            \
    const size_t n = FRAME_SAMPLES;                                         \
    const int threshold = FRAME_THRESHOLD(max);                             \
    type *bg = aligned_alloc(64, n * sizeof(type));                         \
    type *src = aligned_alloc(64, n * sizeof(type));                        \
    type *alpha = aligned_alloc(64, 2 * n * sizeof(type));                  \
    type *dst0 = aligned_alloc(64, n * sizeof(type));                       \
    type *dst1 = aligned_alloc(64, n * sizeof(type));                       \
                                                                            \
    declare_func(void, type *dst, const type *bg, const type *src,          \
                 const type *alpha, size_t hsize, int threshold);           \
                                                                            \
    if (!bg || !src || !alpha || !dst0 || !dst1) {                          \
        fail();                                                             \
    } else {                                                                \
        for (size_t i = 0; i < n; i++) {                                    \
            bg[i]  = rnd() & (max);                                         \
            src[i] = rnd() & (max);                                         \
        }                                                                   \
        text_alpha##bits(alpha, n * hsub);                                  \
                                                                            \
        call_ref(dst0, bg, src, alpha, n, (max) - threshold);               \
        call_new(dst1, bg, src, alpha, n, (max) - threshold);               \
        if (memcmp(dst0, dst1, n * sizeof(type)))                           \
            fail();                                                         \
        call_ref(dst0, bg, src, alpha, n, threshold);                       \
        call_new(dst1, bg, src, alpha, n, threshold);                       \
        if (memcmp(dst0, dst1, n * sizeof(type)))                           \
            fail();                                                         \
                                                                            \
        if (func != ref)                                                    \
            bench_new(dst1, bg, src, alpha, n, threshold);                  \
    }                                                                       \
                                                                            \
    free(bg);                                                               \
    free(src);                                                              \
    free(alpha);                                                            \
    free(dst0);                                                             \
    free(dst1);                                                             \
}                                                                           \
                                                                            \
static void check_frame_inplace##bits(void (*func)(type *, const type *,    \
                                                   const type *, size_t,    \
                                                   int),                    \
                                      void (*ref)(type *, const type *,     \
                                                  const type *, size_t,     \
                                                  int),                     \
                                      const char *name, uint8_t hsub)       \
{                                                                           \
    if (!check_func(func, "%s_frame", name))                                \
        return;                                                             \
                                                                            \
    const size_t n = FRAME_SAMPLES;                                         \
    const int threshold = FRAME_THRESHOLD(max);                             \
    type *src = aligned_alloc(64, n * sizeof(type));                        \
    type *alpha = aligned_alloc(64, 2 * n * sizeof(type));                  \
    type *dst0 = aligned_alloc(64, n * sizeof(type));                       \
    type *dst1 = aligned_alloc(64, n * sizeof(type));                       \
                                                                            \
    declare_func(void, type *dst, const type *src, const type *alpha,       \
                 size_t hsize, int threshold);                              \
                                                                            \
    if (!src || !alpha || !dst0 || !dst1) {                                 \
        fail();                                                             \
    } else {                                                                \
        for (size_t i = 0; i < n; i++) {                                    \
            dst0[i] = dst1[i] = rnd() & (max);                              \
            src[i] = rnd() & (max);                                         \
        }                                                                   \
        text_alpha##bits(alpha, n * hsub);                                  \
                                                                            \
        call_ref(dst0, src, alpha, n, (max) - threshold);                   \
        call_new(dst1, src, alpha, n, (max) - threshold);                   \
        if (memcmp(dst0, dst1, n * sizeof(type)))                           \
            fail();                                                         \
        call_ref(dst0, src, alpha, n, threshold);                           \
        call_new(dst1, src, alpha, n, threshold);                           \
        if (memcmp(dst0, dst1, n * sizeof(type)))                           \
            fail();                                                         \
                                                                            \
        if (func != ref)                                                    \
            bench_new(dst1, src, alpha, n, threshold);                      \
    }                                                                       \
                                                                            \
    free(src);                                                              \
    free(alpha);                                                            \
    free(dst0);                                                             \
    free(dst1);                                                             \
}

CHECK_FRAME(8, uint8_t, 0xff)
CHECK_FRAME(10, uint16_t, 0x3ff)

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
    /* [hsub - 1] */
    void (*inplace8[2])(uint8_t *, const uint8_t *, const uint8_t *, size_t,
                        int) = {
        blit8_inplace_hsub1_ref, blit8_inplace_hsub2_ref };
    void (*inplace10[2])(uint16_t *, const uint16_t *, const uint16_t *,
                         size_t, int) = {
        blit10_inplace_hsub1_ref, blit10_inplace_hsub2_ref };
    /* candidates for the avx512 hsub1 kernels: blendm, ymm */
    void (*candidate8[2])(uint8_t *, const uint8_t *, const uint8_t *,
                          const uint8_t *, size_t, int) = {
        blit8_threshold_hsub1_ref, blit8_threshold_hsub1_ref };
    void (*candidate10[2])(uint16_t *, const uint16_t *, const uint16_t *,
                           const uint16_t *, size_t, int) = {
        blit10_threshold_hsub1_ref, blit10_threshold_hsub1_ref };

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
#ifdef AV_CPU_FLAG_AVX512
    if (cpu_flags & AV_CPU_FLAG_AVX512) {
        threshold8_hsub1 = upipe_blit8_threshold_hsub1_avx512;
        threshold8_hsub2 = upipe_blit8_threshold_hsub2_avx512;
        threshold10_hsub1 = upipe_blit10_threshold_hsub1_avx512;
        threshold10_hsub2 = upipe_blit10_threshold_hsub2_avx512;
        inplace8[0] = upipe_blit8_threshold_inplace_hsub1_avx512;
        inplace8[1] = upipe_blit8_threshold_inplace_hsub2_avx512;
        inplace10[0] = upipe_blit10_threshold_inplace_hsub1_avx512;
        inplace10[1] = upipe_blit10_threshold_inplace_hsub2_avx512;
        candidate8[0] = upipe_blit8_threshold_blendm_hsub1_avx512;
        candidate8[1] = upipe_blit8_threshold_ymm_hsub1_avx512;
        candidate10[0] = upipe_blit10_threshold_blendm_hsub1_avx512;
        candidate10[1] = upipe_blit10_threshold_ymm_hsub1_avx512;
    }
#endif
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

    static const char *const candidate_names[2] = { "blendm", "ymm" };
    char name[64];

    for (int h = 0; h < 2; h++) {
        snprintf(name, sizeof name, "blit8_threshold_inplace_hsub%d", h + 1);
        check_inplace8(inplace8[h], name);
        report("%s", name);
        snprintf(name, sizeof name, "blit10_threshold_inplace_hsub%d", h + 1);
        check_inplace10(inplace10[h], name);
        report("%s", name);
    }

    for (int c = 0; c < 2; c++) {
        snprintf(name, sizeof name, "blit8_threshold_%s_hsub1",
                 candidate_names[c]);
        check_threshold8(candidate8[c], name);
        report("%s", name);
        snprintf(name, sizeof name, "blit10_threshold_%s_hsub1",
                 candidate_names[c]);
        check_threshold10(candidate10[c], name);
        report("%s", name);
    }

    /* frame sized, text alpha */
    void (*const ref8[2])(uint8_t *, const uint8_t *, const uint8_t *,
                          const uint8_t *, size_t, int) = {
        blit8_threshold_hsub1_ref, blit8_threshold_hsub2_ref };
    void (*const ref10[2])(uint16_t *, const uint16_t *, const uint16_t *,
                           const uint16_t *, size_t, int) = {
        blit10_threshold_hsub1_ref, blit10_threshold_hsub2_ref };
    void (*const ref_inplace8[2])(uint8_t *, const uint8_t *, const uint8_t *,
                                  size_t, int) = {
        blit8_inplace_hsub1_ref, blit8_inplace_hsub2_ref };
    void (*const ref_inplace10[2])(uint16_t *, const uint16_t *,
                                   const uint16_t *, size_t, int) = {
        blit10_inplace_hsub1_ref, blit10_inplace_hsub2_ref };
    void (*const fused8[2])(uint8_t *, const uint8_t *, const uint8_t *,
                            const uint8_t *, size_t, int) = {
        threshold8_hsub1, threshold8_hsub2 };
    void (*const fused10[2])(uint16_t *, const uint16_t *, const uint16_t *,
                             const uint16_t *, size_t, int) = {
        threshold10_hsub1, threshold10_hsub2 };

    for (int h = 0; h < 2; h++) {
        snprintf(name, sizeof name, "blit8_threshold_hsub%d", h + 1);
        check_frame8(fused8[h], ref8[h], name, h + 1);
        report("%s_frame", name);
        snprintf(name, sizeof name, "blit10_threshold_hsub%d", h + 1);
        check_frame10(fused10[h], ref10[h], name, h + 1);
        report("%s_frame", name);
        snprintf(name, sizeof name, "blit8_threshold_inplace_hsub%d", h + 1);
        check_frame_inplace8(inplace8[h], ref_inplace8[h], name, h + 1);
        report("%s_frame", name);
        snprintf(name, sizeof name, "blit10_threshold_inplace_hsub%d", h + 1);
        check_frame_inplace10(inplace10[h], ref_inplace10[h], name, h + 1);
        report("%s_frame", name);
    }

    for (int c = 0; c < 2; c++) {
        snprintf(name, sizeof name, "blit8_threshold_%s_hsub1",
                 candidate_names[c]);
        check_frame8(candidate8[c], ref8[0], name, 1);
        report("%s_frame", name);
        snprintf(name, sizeof name, "blit10_threshold_%s_hsub1",
                 candidate_names[c]);
        check_frame10(candidate10[c], ref10[0], name, 1);
        report("%s_frame", name);
    }
}

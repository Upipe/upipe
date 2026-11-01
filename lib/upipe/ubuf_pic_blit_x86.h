#include <emmintrin.h>
#include <tmmintrin.h>
#include <immintrin.h>

#include <stdbool.h>

#include "x86/ubuf_pic_blit.h"

/* gcc < 8 has no avx512 names for __builtin_cpu_supports: the avx512
 * kernels are not used there */
#if defined(__clang__) || __GNUC__ >= 8
#define BLIT_CPU_AVX512_NAMES 1
#endif

static inline bool has_avx512_support(void)
{
#ifdef BLIT_CPU_AVX512_NAMES
    return __builtin_cpu_supports("avx512f")
        && __builtin_cpu_supports("avx512cd")
        && __builtin_cpu_supports("avx512bw")
        && __builtin_cpu_supports("avx512dq")
        && __builtin_cpu_supports("avx512vl");
#else
    return false;
#endif
}

static inline bool has_avx512icl_support(void)
{
#ifdef BLIT_CPU_AVX512_NAMES
    return has_avx512_support()
        && __builtin_cpu_supports("avx512vnni")
        && __builtin_cpu_supports("avx512ifma")
        && __builtin_cpu_supports("avx512vbmi")
        && __builtin_cpu_supports("avx512vbmi2")
        && __builtin_cpu_supports("avx512vpopcntdq")
        && __builtin_cpu_supports("avx512bitalg")
        && __builtin_cpu_supports("gfni");
#else
    return false;
#endif
}

/* __builtin_cpu_supports is too slow to run per row */
static bool blit_avx512;
static bool blit_avx512icl;

__attribute__((constructor)) static void blit_avx512_init(void)
{
    __builtin_cpu_init();
    blit_avx512 = has_avx512_support();
    blit_avx512icl = has_avx512icl_support();
}

/*
 * @This handle particular blit cases.  Exists to avoid more nested ifs.
 *
 * @param dst destination pointer
 * @param bg background pointer, equal to dst when blending in place
 * @param src source pointer
 * @param alpha alpha plane pointer
 * @param alpha_multiplier alpha multiplier
 * @param threshold alpha blending method
 * @param hsize width in samples
 * @param hsub source and destination plane horizontal subsampling
 *
 * TODO: whole planes
 */

static bool blit8_handle_cases(uint8_t *dst, const uint8_t *bg,
        const uint8_t *src, const uint8_t *alpha, int alpha_multiplier,
        int threshold, size_t hsize, uint8_t hsub)
{
    /* Duplicating a cases not handled here to mirror the logic in caller. */
    if ((!alpha && alpha_multiplier == 0xff) || threshold == 0) {
        /* Caller does a memcpy */
        return false;
    }
    else if (!alpha) {
        /* Caller does a blend */
        return false;
    }
    else if (hsize < 32) {
        /* Let caller handle small blits */
        return false;
    }

    else if (threshold != 0xff && alpha_multiplier == 0xff) {
        if (hsub == 1) {
            if (blit_avx512) {
                if (dst == bg)
                    upipe_blit8_threshold_inplace_hsub1_avx512(dst, src, alpha, hsize, threshold);
                else
                    upipe_blit8_threshold_hsub1_avx512(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                if (dst == bg)
                    upipe_blit8_threshold_inplace_hsub1_avx2(dst, src, alpha, hsize, threshold);
                else
                    upipe_blit8_threshold_hsub1_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
            if (blit_avx512) {
                if (dst == bg)
                    upipe_blit8_threshold_inplace_hsub2_avx512(dst, src, alpha, hsize, threshold);
                else
                    upipe_blit8_threshold_hsub2_avx512(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit8_threshold_hsub2_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }
        return false;
    }

    else if (threshold == 0xff && alpha_multiplier == 0xff) {
        if (hsub == 1) {
            if (dst == bg && blit_avx512) {
                upipe_blit8_blend_inplace_hsub1_avx512(dst, src, alpha, hsize);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                if (dst == bg)
                    upipe_blit8_blend_inplace_hsub1_avx2(dst, src, alpha, hsize);
                else
                    upipe_blit8_blend_hsub1_avx2(dst, bg, src, alpha, hsize);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
            if (dst == bg && blit_avx512) {
                upipe_blit8_blend_inplace_hsub2_avx512(dst, src, alpha, hsize);
                return true;
            }
            if (blit_avx512icl) {
                upipe_blit8_blend_hsub2_avx512icl(dst, bg, src, alpha, hsize);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit8_blend_hsub2_avx2(dst, bg, src, alpha, hsize);
                return true;
            }
            return false;
        }
        return false;
    }
    return false;
}

/*
 * @This handle particular blit cases.  Exists to avoid more nested ifs.
 *
 * @param dst destination pointer
 * @param bg background pointer, equal to dst when blending in place
 * @param src source pointer
 * @param alpha alpha plane pointer
 * @param alpha_multiplier alpha multiplier
 * @param threshold alpha blending method
 * @param hsize width in samples
 * @param hsub source and destination plane horizontal subsampling
 *
 * TODO: whole planes
 */

static bool blit10_handle_cases(uint16_t *dst, const uint16_t *bg,
        const uint16_t *src, const uint16_t *alpha, int alpha_multiplier,
        int threshold, size_t hsize, uint8_t hsub)
{
    /* Duplicating a cases not handled here to mirror the logic in caller. */
    if ((!alpha && alpha_multiplier == 0x3ff) || threshold == 0) {
        /* Caller does a memcpy */
        return false;
    }
    else if (!alpha) {
        /* Caller does a blend */
        return false;
    }
    else if (hsize < 16) {
        /* Let caller handle small blits */
        return false;
    }

    else if (threshold != 0x3ff && alpha_multiplier == 0x3ff) {
        if (hsub == 1) {
            if (blit_avx512) {
                if (dst == bg)
                    upipe_blit10_threshold_inplace_hsub1_avx512(dst, src, alpha, hsize, threshold);
                else
                    upipe_blit10_threshold_hsub1_avx512(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                if (dst == bg)
                    upipe_blit10_threshold_inplace_hsub1_avx2(dst, src, alpha, hsize, threshold);
                else
                    upipe_blit10_threshold_hsub1_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
            if (blit_avx512) {
                if (dst == bg)
                    upipe_blit10_threshold_inplace_hsub2_avx512(dst, src, alpha, hsize, threshold);
                else
                    upipe_blit10_threshold_hsub2_avx512(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit10_threshold_hsub2_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }
        return false;
    }

    else if (threshold == 0x3ff && alpha_multiplier == 0x3ff) {
        if (hsub == 1) {
            if (dst == bg && blit_avx512) {
                upipe_blit10_blend_inplace_hsub1_avx512(dst, src, alpha, hsize);
                return true;
            }
            if (dst != bg && blit_avx512icl) {
                upipe_blit10_blend_hsub1_avx512icl(dst, bg, src, alpha, hsize);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                if (dst == bg)
                    upipe_blit10_blend_inplace_hsub1_avx2(dst, src, alpha, hsize);
                else
                    upipe_blit10_blend_hsub1_avx2(dst, bg, src, alpha, hsize);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
            if (dst == bg && blit_avx512) {
                upipe_blit10_blend_inplace_hsub2_avx512(dst, src, alpha, hsize);
                return true;
            }
            if (blit_avx512icl) {
                upipe_blit10_blend_hsub2_avx512icl(dst, bg, src, alpha, hsize);
                return true;
            }
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit10_blend_hsub2_avx2(dst, bg, src, alpha, hsize);
                return true;
            }
            return false;
        }
        return false;
    }
    return false;
}

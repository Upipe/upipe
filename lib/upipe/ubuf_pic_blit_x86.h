#include <emmintrin.h>
#include <tmmintrin.h>
#include <immintrin.h>

#include "x86/ubuf_pic_blit.h"

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
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit8_threshold_hsub1_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("ssse3")) {
                upipe_blit8_threshold_hsub1_ssse3(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit8_threshold_hsub2_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("ssse3")) {
                upipe_blit8_threshold_hsub2_ssse3(dst, bg, src, alpha, hsize, threshold);
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
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit10_threshold_hsub1_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("ssse3")) {
                upipe_blit10_threshold_hsub1_ssse3(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
            if (__builtin_cpu_supports("avx2")) {
                upipe_blit10_threshold_hsub2_avx2(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            if (__builtin_cpu_supports("ssse3")) {
                upipe_blit10_threshold_hsub2_ssse3(dst, bg, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }
        return false;
    }
    return false;
}

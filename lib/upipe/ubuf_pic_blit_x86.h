#include <emmintrin.h>
#include <tmmintrin.h>
#include <immintrin.h>

/*
 * It might be easier to make these external assembly, other than handling the
 * tail as scalar.  gcc cannot inline functions built with different options.
 * See this quote from the manual.
 *
 * On the x86, the inliner does not inline a function that has different target
 * options than the caller, unless the callee has a subset of the target options
 * of the caller. For example a function declared with target("sse3") can inline
 * a function with target("sse2"), since -msse3 implies -msse2.
 */

/*
 * SSE
 */

__attribute__((target("sse2")))
static void blit10_threshold_hsub1_sse2(uint16_t *real_dst,
        const uint16_t *real_src, const uint16_t *real_alpha,
        size_t plane_hsize, int threshold)
{
    size_t j = 0;
    const __m128i t = _mm_set1_epi16(threshold);
    for (j = 0; (j+8) <= plane_hsize; j += 8) {
        __m128i dst = _mm_loadu_si128((void*)(real_dst + j));
        __m128i src = _mm_loadu_si128((void*)(real_src + j));
        __m128i alp = _mm_loadu_si128((void*)(real_alpha + j));
        __m128i msk = _mm_cmpgt_epi16(alp, t);
        src = _mm_and_si128(src, msk);
        dst = _mm_andnot_si128(msk, dst);
        _mm_storeu_si128((void*)(real_dst + j), _mm_or_si128(dst, src));
    }
    for (/* nothing */; j < plane_hsize; j++) {
        const uint16_t a = real_alpha[j];
        if (a > threshold) real_dst[j] = real_src[j];
    }
}

__attribute__((target("ssse3")))
static void blit10_threshold_hsub2_ssse3(uint16_t *real_dst,
        const uint16_t *real_src, const uint16_t *real_alpha,
        size_t plane_hsize, int threshold)
{
    size_t j = 0;
    const __m128i t = _mm_set1_epi16(threshold);
    const __m128i shuf = _mm_setr_epi8(0,1, 4,5, 8,9, 12,13, -1,-1,-1,-1,-1,-1,-1,-1);
    for (j = 0; (j+8) <= plane_hsize; j += 8) {
        __m128i dst = _mm_loadu_si128((void*)(real_dst + j));
        __m128i src = _mm_loadu_si128((void*)(real_src + j));
        /* Load and subsample alpha by selecting every 2nd word */
        __m128i alp0 = _mm_loadu_si128((void*)(real_alpha + 2*j));
        __m128i alp1 = _mm_loadu_si128((void*)(real_alpha + 2*j+8));
        alp0 = _mm_shuffle_epi8(alp0, shuf);
        alp1 = _mm_shuffle_epi8(alp1, shuf);
        __m128i alp = _mm_unpacklo_epi64(alp0, alp1);
        /* Use alpha as normal */
        __m128i msk = _mm_cmpgt_epi16(alp, t);
        src = _mm_and_si128(src, msk);
        dst = _mm_andnot_si128(msk, dst);
        _mm_storeu_si128((void*)(real_dst + j), _mm_or_si128(dst, src));
    }
    for (/* nothing */; j < plane_hsize; j++) {
        const uint16_t a = real_alpha[j * 2];
        if (a > threshold) real_dst[j] = real_src[j];
    }
}

/*
 * AVX2
 */

#if __GNUC__ >= 5

__attribute__((target("avx2")))
static void blit10_threshold_hsub1_avx2(uint16_t *real_dst,
        const uint16_t *real_src, const uint16_t *real_alpha,
        size_t plane_hsize, int threshold)
{
    size_t j = 0;
    const __m256i t = _mm256_set1_epi16(threshold);
    for (j = 0; (j+16) <= plane_hsize; j += 16) {
        __m256i dst = _mm256_loadu_si256((void*)(real_dst + j));
        __m256i src = _mm256_loadu_si256((void*)(real_src + j));
        __m256i alp = _mm256_loadu_si256((void*)(real_alpha + j));
        __m256i msk = _mm256_cmpgt_epi16(alp, t);
        src = _mm256_and_si256(src, msk);
        dst = _mm256_andnot_si256(msk, dst);
        _mm256_storeu_si256((void*)(real_dst + j), _mm256_or_si256(dst, src));
    }
    for (/* nothing */; j < plane_hsize; j++) {
        const uint16_t a = real_alpha[j];
        if (a > threshold) real_dst[j] = real_src[j];
    }
}

__attribute__((target("avx2")))
static void blit10_threshold_hsub2_avx2(uint16_t *real_dst,
        const uint16_t *real_src, const uint16_t *real_alpha,
        size_t plane_hsize, int threshold)
{
    size_t j = 0;
    const __m256i t = _mm256_set1_epi16(threshold);
    const __m256i shuf = _mm256_broadcastsi128_si256(_mm_setr_epi8(0,1, 4,5, 8,9, 12,13, -1,-1,-1,-1,-1,-1,-1,-1));
    for (j = 0; (j+16) <= plane_hsize; j += 16) {
        __m256i dst = _mm256_loadu_si256((void*)(real_dst + j));
        __m256i src = _mm256_loadu_si256((void*)(real_src + j));
        /* Load and subsample alpha by selecting every 2nd word */
        __m256i alp0 = _mm256_loadu_si256((void*)(real_alpha + 2*j));
        __m256i alp1 = _mm256_loadu_si256((void*)(real_alpha + 2*j+16));
        alp0 = _mm256_shuffle_epi8(alp0, shuf);
        alp1 = _mm256_shuffle_epi8(alp1, shuf);
        __m256i alp = _mm256_permute4x64_epi64(_mm256_unpacklo_epi64(alp0, alp1), 0|2<<2|1<<4|3<<6);
        /* Use alpha as normal */
        __m256i msk = _mm256_cmpgt_epi16(alp, t);
        src = _mm256_and_si256(src, msk);
        dst = _mm256_andnot_si256(msk, dst);
        _mm256_storeu_si256((void*)(real_dst + j), _mm256_or_si256(dst, src));
    }
    for (/* nothing */; j < plane_hsize; j++) {
        const uint16_t a = real_alpha[j * 2];
        if (a > threshold) real_dst[j] = real_src[j];
    }
}

#endif /* if __GNUC__ >= 5 */

/*
 * @This handle particular blit cases.  Exists to avoid more nested ifs.
 *
 * @param dst destination pointer
 * @param src source pointer
 * @param alpha alpha plane pointer
 * @param alpha_multiplier alpha multiplier
 * @param threshold alpha blending method
 * @param hsize width in samples
 * @param hsub source and destination plane horizontal subsampling
 *
 * TODO: whole planes
 */

static inline bool blit10_handle_cases(uint16_t *dst, const uint16_t *src,
        const uint16_t *alpha, int alpha_multiplier, int threshold,
        size_t hsize, uint8_t hsub)
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
    else if (hsize < 8) {
        /* Let caller handle small blits */
        return false;
    }

    else if (threshold != 0x3ff && alpha_multiplier == 0x3ff) {
        if (hsub == 1) {
#if __GNUC__ >= 5
            if (__builtin_cpu_supports("avx2")) {
                blit10_threshold_hsub1_avx2(dst, src, alpha, hsize, threshold);
                return true;
            }
#endif
            if (__builtin_cpu_supports("sse2")) {
                blit10_threshold_hsub1_sse2(dst, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }

        if (hsub == 2) {
#if __GNUC__ >= 5
            if (__builtin_cpu_supports("avx2")) {
                blit10_threshold_hsub2_avx2(dst, src, alpha, hsize, threshold);
                return true;
            }
#endif
            if (__builtin_cpu_supports("ssse3")) {
                blit10_threshold_hsub2_ssse3(dst, src, alpha, hsize, threshold);
                return true;
            }
            return false;
        }
        return false;
    }
    return false;
}

/* dst = alpha > threshold ? src : bg.  Pass bg == dst to blend in place. */
void upipe_blit8_threshold_hsub1_avx2(uint8_t *dst_plane, const uint8_t *bg_plane,
        const uint8_t *src_plane,
        const uint8_t *alpha_plane, size_t hsize, int threshold);
void upipe_blit8_threshold_hsub2_avx2(uint8_t *dst_plane, const uint8_t *bg_plane,
        const uint8_t *src_plane,
        const uint8_t *alpha_plane, size_t hsize, int threshold);
void upipe_blit10_threshold_hsub1_avx2(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize, int threshold);
void upipe_blit10_threshold_hsub2_avx2(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize, int threshold);

void upipe_blit8_blend_hsub1_avx2(uint8_t *dst_plane, const uint8_t *bg_plane,
        const uint8_t *src_plane,
        const uint8_t *alpha_plane, size_t hsize);
void upipe_blit8_blend_hsub2_avx2(uint8_t *dst_plane, const uint8_t *bg_plane,
        const uint8_t *src_plane,
        const uint8_t *alpha_plane, size_t hsize);

/* dst = (bg * (0x3ff - alpha) + src * alpha) / 0x3ff, i.e. the maximum
 * threshold, blending with the alpha levels.  Pass bg == dst to blend in
 * place.  There is no threshold argument: it is implied by the method. */
void upipe_blit10_blend_hsub1_avx2(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize);
void upipe_blit10_blend_hsub2_avx2(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize);
/* the same with AVX512-VNNI, in 256 bits */
void upipe_blit10_blend_hsub1_avx512icl(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize);
void upipe_blit10_blend_hsub2_avx512icl(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize);

/* The avx512 kernels predicate their tail, so they accept any hsize.  The hsub1
 * ones work in 256 bits. */
void upipe_blit8_threshold_hsub1_avx512(uint8_t *dst_plane, const uint8_t *bg_plane,
        const uint8_t *src_plane,
        const uint8_t *alpha_plane, size_t hsize, int threshold);
void upipe_blit8_threshold_hsub2_avx512(uint8_t *dst_plane, const uint8_t *bg_plane,
        const uint8_t *src_plane,
        const uint8_t *alpha_plane, size_t hsize, int threshold);
void upipe_blit10_threshold_hsub1_avx512(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize, int threshold);
void upipe_blit10_threshold_hsub2_avx512(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize, int threshold);

/* In place: dst = alpha > threshold ? src : dst, leaving a vector with no
 * selected sample unwritten. */
void upipe_blit8_threshold_inplace_hsub1_avx512(uint8_t *dst_plane,
        const uint8_t *src_plane, const uint8_t *alpha_plane, size_t hsize,
        int threshold);
void upipe_blit8_threshold_inplace_hsub2_avx512(uint8_t *dst_plane,
        const uint8_t *src_plane, const uint8_t *alpha_plane, size_t hsize,
        int threshold);
void upipe_blit10_threshold_inplace_hsub1_avx512(uint16_t *dst_plane,
        const uint16_t *src_plane, const uint16_t *alpha_plane, size_t hsize,
        int threshold);
void upipe_blit10_threshold_inplace_hsub2_avx512(uint16_t *dst_plane,
        const uint16_t *src_plane, const uint16_t *alpha_plane, size_t hsize,
        int threshold);

/* In place, bg == dst, avx2, hsub1: a vector with nothing to change is neither
 * loaded nor stored. */
void upipe_blit8_threshold_inplace_hsub1_avx2(uint8_t *dst_plane,
        const uint8_t *src_plane, const uint8_t *alpha_plane, size_t hsize,
        int threshold);
void upipe_blit10_threshold_inplace_hsub1_avx2(uint16_t *dst_plane,
        const uint16_t *src_plane, const uint16_t *alpha_plane, size_t hsize,
        int threshold);
void upipe_blit8_blend_inplace_hsub1_avx2(uint8_t *dst_plane,
        const uint8_t *src_plane, const uint8_t *alpha_plane, size_t hsize);
void upipe_blit10_blend_inplace_hsub1_avx2(uint16_t *dst_plane,
        const uint16_t *src_plane, const uint16_t *alpha_plane, size_t hsize);

/* Blend in place, bg == dst: all transparent vectors skipped, all opaque ones
 * copied. */
void upipe_blit8_blend_inplace_hsub1_avx512(uint8_t *dst_plane,
        const uint8_t *src_plane, const uint8_t *alpha_plane, size_t hsize);
void upipe_blit8_blend_inplace_hsub2_avx512(uint8_t *dst_plane,
        const uint8_t *src_plane, const uint8_t *alpha_plane, size_t hsize);
void upipe_blit10_blend_inplace_hsub1_avx512(uint16_t *dst_plane,
        const uint16_t *src_plane, const uint16_t *alpha_plane, size_t hsize);
void upipe_blit10_blend_inplace_hsub2_avx512(uint16_t *dst_plane,
        const uint16_t *src_plane, const uint16_t *alpha_plane, size_t hsize);

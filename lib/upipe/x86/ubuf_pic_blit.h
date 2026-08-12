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

/* dst = (bg * (0x3ff - alpha) + src * alpha) / 0x3ff, i.e. the maximum
 * threshold, blending with the alpha levels.  Pass bg == dst to blend in
 * place.  There is no threshold argument: it is implied by the method. */
void upipe_blit10_blend_hsub1_avx2(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize);
void upipe_blit10_blend_hsub2_avx2(uint16_t *dst_plane, const uint16_t *bg_plane,
        const uint16_t *src_plane,
        const uint16_t *alpha_plane, size_t hsize);

void upipe_v210_to_planar_10_aligned_ssse3(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t width);
void upipe_v210_to_planar_10_aligned_avx  (const void *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t width);
void upipe_v210_to_planar_10_aligned_avx2 (const void *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t width);

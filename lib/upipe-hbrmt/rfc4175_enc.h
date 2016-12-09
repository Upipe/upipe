/* process (6*mmsize)/16 pixels per iteration */
void upipe_v210_to_planar_10_aligned_ssse3(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t pixels);
void upipe_v210_to_planar_10_aligned_avx  (const void *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t pixels);
void upipe_v210_to_planar_10_aligned_avx2 (const void *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_v210_to_planar_8_aligned_ssse3(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);
void upipe_v210_to_planar_8_aligned_avx  (const void *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);
void upipe_v210_to_planar_8_aligned_avx2 (const void *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);

void upipe_v210_to_sdi_ssse3(uint32_t *src, uint8_t *dst, int64_t width);
void upipe_v210_to_sdi_avx  (uint32_t *src, uint8_t *dst, int64_t width);
void upipe_v210_to_sdi_avx2 (uint32_t *src, uint8_t *dst, int64_t width);

void upipe_uyvy_to_sdi_c(uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_2_c(uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels);
void upipe_v210_to_uyvy_c(const uint32_t *src, uint16_t *dst, uintptr_t pixels);

/* process mmsize/2 samples per iteration */
void upipe_uyvy_to_sdi_aligned_ssse3(uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_unaligned_ssse3(uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_avx  (uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_avx2 (uint8_t *dst, const uint8_t *y, uintptr_t pixels);

void upipe_uyvy_to_sdi_2_aligned_ssse3  (uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_2_unaligned_ssse3(uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_2_avx            (uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_2_avx2           (uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels);

void upipe_planar_to_uyvy_8_c(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const uintptr_t width);
/* process 2*mmsize pixels per iteration */
void upipe_planar_to_uyvy_8_aligned_sse2  (uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_8_aligned_avx   (uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_8_aligned_avx2  (uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_8_unaligned_sse2(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_8_unaligned_avx (uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_8_unaligned_avx2(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, uintptr_t pixels);

void upipe_planar_to_uyvy_10_c(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const uintptr_t width);
/* process mmsize pixels per iteration */
void upipe_planar_to_uyvy_10_aligned_sse2  (uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_10_aligned_avx   (uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_10_aligned_avx2  (uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_10_unaligned_sse2(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_10_unaligned_avx (uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels);
void upipe_planar_to_uyvy_10_unaligned_avx2(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, uintptr_t pixels);

/* process (3*mmsize)/4 pixels per iteration */
void upipe_v210_to_uyvy_aligned_ssse3(const uint32_t *src, uint16_t *uyvy, uintptr_t pixels);
void upipe_v210_to_uyvy_aligned_avx  (const uint32_t *src, uint16_t *uyvy, uintptr_t pixels);
void upipe_v210_to_uyvy_aligned_avx2 (const uint32_t *src, uint16_t *uyvy, uintptr_t pixels);
void upipe_v210_to_uyvy_unaligned_ssse3(const uint32_t *src, uint16_t *uyvy, uintptr_t pixels);
void upipe_v210_to_uyvy_unaligned_avx  (const uint32_t *src, uint16_t *uyvy, uintptr_t pixels);
void upipe_v210_to_uyvy_unaligned_avx2 (const uint32_t *src, uint16_t *uyvy, uintptr_t pixels);

void upipe_sdi_blank_sse(uint16_t *dst, uintptr_t pixels);
void upipe_sdi_blank_avx(uint16_t *dst, uintptr_t pixels);

/* process mmsize samples per iteration */
void upipe_planar_10_to_planar_8_sse2(uint16_t *data_10, const uint8_t *data_8, uintptr_t samples);
void upipe_planar_10_to_planar_8_avx2(uint16_t *data_10, const uint8_t *data_8, uintptr_t samples);

/* process mmsize/2 samples per iteration */
void upipe_planar8_to_planar10_sse2(uint16_t *data_10, const uint8_t *data_8, uintptr_t samples);
void upipe_planar8_to_planar10_avx2(uint16_t *data_10, const uint8_t *data_8, uintptr_t samples);

/* process mmsize/2 samples per iteration */
void upipe_uyvy_to_sdi_aligned_ssse3(uint8_t *dst, const uint8_t *y, int64_t pixels);
void upipe_uyvy_to_sdi_unaligned_ssse3(uint8_t *dst, const uint8_t *y, int64_t pixels);
void upipe_uyvy_to_sdi_avx  (uint8_t *dst, const uint8_t *y, int64_t pixels);
void upipe_uyvy_to_sdi_avx2 (uint8_t *dst, const uint8_t *y, int64_t pixels);

/* process 2*mmsize pixels per iteration */
void upipe_planar_to_uyvy_8_sse2(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t pixels);
void upipe_planar_to_uyvy_8_avx (uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t pixels);
void upipe_planar_to_uyvy_8_avx2(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t pixels);

/* process mmsize pixels per iteration */
void upipe_planar_to_uyvy_10_sse2(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t pixels);
void upipe_planar_to_uyvy_10_avx (uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t pixels);
void upipe_planar_to_uyvy_10_avx2(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t pixels);

/* process (3*mmsize)/4 pixels per iteration */
void upipe_v210_to_uyvy_aligned_ssse3(const uint32_t *src, uint16_t *uyvy, int64_t pixels);
void upipe_v210_to_uyvy_aligned_avx  (const uint32_t *src, uint16_t *uyvy, int64_t pixels);
void upipe_v210_to_uyvy_aligned_avx2 (const uint32_t *src, uint16_t *uyvy, int64_t pixels);
void upipe_v210_to_uyvy_unaligned_ssse3(const uint32_t *src, uint16_t *uyvy, int64_t pixels);
void upipe_v210_to_uyvy_unaligned_avx  (const uint32_t *src, uint16_t *uyvy, int64_t pixels);
void upipe_v210_to_uyvy_unaligned_avx2 (const uint32_t *src, uint16_t *uyvy, int64_t pixels);

void upipe_sdi_blank_avx(uint16_t *dst, int64_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_planar_to_sdi_8_ssse3(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dest, int64_t pixels);
void upipe_planar_to_sdi_8_avx  (const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dest, int64_t pixels);
void upipe_planar_to_sdi_8_avx2 (const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dest, int64_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_planar_to_sdi_10_ssse3(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dest, int64_t pixels);
void upipe_planar_to_sdi_10_avx  (const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dest, int64_t pixels);
void upipe_planar_to_sdi_10_avx2 (const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dest, int64_t pixels);

/* process mmsize samples per iteration */
void upipe_planar_10_to_planar_8_sse2(uint16_t *data_10, const uint8_t *data_8, int64_t samples);
void upipe_planar_10_to_planar_8_avx2(uint16_t *data_10, const uint8_t *data_8, int64_t samples);

/* process mmsize/2 samples per iteration */
void upipe_planar8_to_planar10_sse2(uint16_t *data_10, const uint8_t *data_8, int64_t samples);
void upipe_planar8_to_planar10_avx2(uint16_t *data_10, const uint8_t *data_8, int64_t samples);

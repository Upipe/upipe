/* process mmsize/4 pixels per iteration */
void upipe_sdi_to_uyvy_aligned_ssse3  (const uint8_t *src, uint16_t *y, int64_t pixels);
void upipe_sdi_to_uyvy_aligned_avx2   (const uint8_t *src, uint16_t *y, int64_t pixels);
void upipe_sdi_to_uyvy_unaligned_ssse3(const uint8_t *src, uint16_t *y, int64_t pixels);
void upipe_sdi_to_uyvy_unaligned_avx2 (const uint8_t *src, uint16_t *y, int64_t pixels);
void upipe_sdi_unpack_c(const uint8_t *src, uint16_t *y, int64_t pixels);

void upipe_sdi_v210_unpack_c(const uint8_t *src, uint32_t *dst, int64_t pixels);

void upipe_sdi_to_planar_8_c(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);

/* process mmsize pixels per iteration */
void upipe_uyvy_to_planar_8_aligned_ssse3  (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_8_aligned_avx    (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_8_aligned_avx2   (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_8_unaligned_ssse3(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_8_unaligned_avx  (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_8_unaligned_avx2 (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t pixels);

/* process mmsize pixels per iteration */
void upipe_uyvy_to_planar_10_aligned_ssse3  (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_10_aligned_avx    (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_10_aligned_avx2   (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_10_unaligned_ssse3(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_10_unaligned_avx  (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t pixels);
void upipe_uyvy_to_planar_10_unaligned_avx2 (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t pixels);

/* process (mmsize*3)/8 pixels per iteration */
void upipe_uyvy_to_v210_aligned_ssse3  (const uint16_t *y, uint8_t *dst, int64_t pixels);
void upipe_uyvy_to_v210_aligned_avx    (const uint16_t *y, uint8_t *dst, int64_t pixels);
void upipe_uyvy_to_v210_aligned_avx2   (const uint16_t *y, uint8_t *dst, int64_t pixels);
void upipe_uyvy_to_v210_unaligned_ssse3(const uint16_t *y, uint8_t *dst, int64_t pixels);
void upipe_uyvy_to_v210_unaligned_avx  (const uint16_t *y, uint8_t *dst, int64_t pixels);
void upipe_uyvy_to_v210_unaligned_avx2 (const uint16_t *y, uint8_t *dst, int64_t pixels);

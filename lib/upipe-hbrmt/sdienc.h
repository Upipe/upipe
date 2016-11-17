/* process mmsize/2 samples per iteration */
void upipe_sdi_pack_10_ssse3(uint8_t *dst, const uint8_t *y, int64_t size);
void upipe_sdi_pack_10_avx  (uint8_t *dst, const uint8_t *y, int64_t size);
void upipe_sdi_pack_10_avx2 (uint8_t *dst, const uint8_t *y, int64_t size);

/* process 2*mmsize pixels per iteration */
void upipe_planar_to_uyvy_8_avx(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t width);
void upipe_planar_to_uyvy_8_avx2(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const int64_t width);

/* process mmsize pixels per iteration */
void upipe_planar_to_uyvy_10_sse2(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t width);
void upipe_planar_to_uyvy_10_avx (uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t width);
void upipe_planar_to_uyvy_10_avx2(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const int64_t width);

/* process (3*mmsize)/4 pixels per iteration */
void upipe_v210_uyvy_unpack_aligned_ssse3(const uint32_t *src, uint16_t *uyvy, int64_t width);
void upipe_v210_uyvy_unpack_aligned_avx  (const uint32_t *src, uint16_t *uyvy, int64_t width);
void upipe_v210_uyvy_unpack_aligned_avx2 (const uint32_t *src, uint16_t *uyvy, int64_t width);
void upipe_v210_uyvy_unpack_unaligned_ssse3(const uint32_t *src, uint16_t *uyvy, int64_t width);
void upipe_v210_uyvy_unpack_unaligned_avx  (const uint32_t *src, uint16_t *uyvy, int64_t width);
void upipe_v210_uyvy_unpack_unaligned_avx2 (const uint32_t *src, uint16_t *uyvy, int64_t width);

void upipe_sdi_blank_avx(uint16_t *dst, int64_t size);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_planar_to_sdi_8_ssse3(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dest, int64_t width);
void upipe_planar_to_sdi_8_avx  (const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dest, int64_t width);
void upipe_planar_to_sdi_8_avx2 (const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dest, int64_t width);

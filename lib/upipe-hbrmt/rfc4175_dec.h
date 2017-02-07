/* process (6*mmsize)/16 pixels per iteration */
void upipe_sdi_to_v210_ssse3(const uint8_t *src, uint32_t *dst, int64_t pixels);
void upipe_sdi_to_v210_avx  (const uint8_t *src, uint32_t *dst, int64_t pixels);
void upipe_sdi_to_v210_avx2 (const uint8_t *src, uint32_t *dst, int64_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_sdi_to_planar_8_ssse3(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);
void upipe_sdi_to_planar_8_avx  (const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);
void upipe_sdi_to_planar_8_avx2 (const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_sdi_to_planar_10_ssse3(const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t pixels);
void upipe_sdi_to_planar_10_avx  (const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t pixels);
void upipe_sdi_to_planar_10_avx2 (const uint8_t *src, uint16_t *y, uint16_t *u, uint16_t *v, int64_t pixels);

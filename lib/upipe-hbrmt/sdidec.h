void upipe_sdi_unpack_10_ssse3(const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_unpack_10_avx2 (const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_unpack_c(const uint8_t *src, uint16_t *y, int64_t size);

void upipe_sdi_v210_unpack_avx(const uint8_t *src, uint32_t *dst, int64_t size);
void upipe_sdi_to_planar_8_avx(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t size);
void upipe_sdi_v210_unpack_c(const uint8_t *src, uint32_t *dst, int64_t size);
void upipe_sdi_to_planar_8_c(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t size);

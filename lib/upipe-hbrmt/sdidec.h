void upipe_sdi_unpack_10_ssse3(const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_unpack_10_avx2 (const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_unpack_c(const uint8_t *src, uint16_t *y, int64_t size);

void upipe_sdi_v210_unpack_avx(const uint8_t *src, uint32_t *dst, int64_t size);
void upipe_sdi_to_planar_8_avx(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t size);
void upipe_sdi_v210_unpack_c(const uint8_t *src, uint32_t *dst, int64_t size);
void upipe_sdi_to_planar_8_c(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, int64_t size);

void upipe_uyvy_to_planar_8_avx(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, const int64_t width);

void upipe_uyvy_to_planar_10_avx(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, const int64_t width);

void upipe_uyvy_to_v210_ssse3(const uint16_t *y, uint8_t *dst, ptrdiff_t width);

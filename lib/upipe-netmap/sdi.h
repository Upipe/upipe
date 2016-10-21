void upipe_planar_to_sdi_8_avx(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width);
void upipe_planar_to_sdi_10_avx(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);
void upipe_v210_sdi_unpack_aligned_avx(const uint32_t *src, uint8_t *sdi, int64_t width);

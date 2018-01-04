void upipe_sdi_unpack_c(const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_unpack_10_ssse3(const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_unpack_10_avx2 (const uint8_t *src, uint16_t *y, int64_t size);

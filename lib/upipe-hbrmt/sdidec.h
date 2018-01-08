void upipe_sdi_to_uyvy_c(const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_to_uyvy_ssse3(const uint8_t *src, uint16_t *y, int64_t size);
void upipe_sdi_to_uyvy_avx2 (const uint8_t *src, uint16_t *y, int64_t size);

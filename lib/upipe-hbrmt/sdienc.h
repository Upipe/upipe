void upipe_uyvy_to_sdi_c(uint8_t *dst, const uint8_t *y, int64_t pixels);
void upipe_uyvy_to_sdi_ssse3(uint8_t *dst, const uint8_t *y, int64_t pixels);
void upipe_uyvy_to_sdi_avx  (uint8_t *dst, const uint8_t *y, int64_t pixels);
void upipe_uyvy_to_sdi_avx2 (uint8_t *dst, const uint8_t *y, int64_t pixels);

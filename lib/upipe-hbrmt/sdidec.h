void upipe_sdi_to_uyvy_c    (const uint8_t *src, uint16_t *y, uintptr_t pixels);
void upipe_sdi_to_uyvy_ssse3(const uint8_t *src, uint16_t *y, uintptr_t pixels);
void upipe_sdi_to_uyvy_avx2 (const uint8_t *src, uint16_t *y, uintptr_t pixels);

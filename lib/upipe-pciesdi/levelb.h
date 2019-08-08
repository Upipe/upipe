void upipe_sdi3g_to_uyvy_2_c(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_sdi3g_to_uyvy_2_ssse3(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_sdi3g_to_uyvy_2_avx(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_sdi3g_to_uyvy_2_avx2(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);

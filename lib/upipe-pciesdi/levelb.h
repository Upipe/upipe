void upipe_levelb_to_uyvy_c(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_levelb_to_uyvy_ssse3(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_levelb_to_uyvy_avx(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);
void upipe_levelb_to_uyvy_avx2(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels);

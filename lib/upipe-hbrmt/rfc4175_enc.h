void upipe_v210_to_sdi_c(const uint32_t *src, uint8_t *dst, uintptr_t pixels);
/* process (6*mmsize)/16 pixels per iteration */
void upipe_v210_to_sdi_ssse3(const uint32_t *src, uint8_t *dst, uintptr_t pixels);
void upipe_v210_to_sdi_avx  (const uint32_t *src, uint8_t *dst, uintptr_t pixels);
void upipe_v210_to_sdi_avx2 (const uint32_t *src, uint8_t *dst, uintptr_t pixels);

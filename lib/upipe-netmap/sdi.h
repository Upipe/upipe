void upipe_planar_to_sdi_8_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width);
void upipe_planar_to_sdi_8_ssse3(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width);
void upipe_planar_to_sdi_8_avx(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width);

void upipe_planar_to_sdi_10_c(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);
void upipe_planar_to_sdi_10_ssse3(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);
void upipe_planar_to_sdi_10_avx(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width);

void upipe_planar_to_sdi_8_2_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
void upipe_planar_to_sdi_8_2_ssse3(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
void upipe_planar_to_sdi_8_2_avx(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);

void upipe_planar_to_sdi_10_2_c(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
void upipe_planar_to_sdi_10_2_ssse3(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);
void upipe_planar_to_sdi_10_2_avx(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst1, uint8_t *dst2, uintptr_t pixels);

void upipe_sdi_vanc_deinterleave_ssse3(void *vanc_buf, ptrdiff_t vanc_stride, const void *source, ptrdiff_t src_stride);

void upipe_sdi_to_uyvy_c(const uint8_t *src, uint16_t *y, uintptr_t pixels);
/* process mmsize/4 pixels per iteration */
void upipe_sdi_to_uyvy_ssse3(const uint8_t *src, uint16_t *y, uintptr_t pixels);
void upipe_sdi_to_uyvy_avx2 (const uint8_t *src, uint16_t *y, uintptr_t pixels);

void upipe_uyvy_to_planar_8_c(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, uintptr_t width);
/* process mmsize pixels per iteration */
void upipe_uyvy_to_planar_8_ssse3(uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, uintptr_t pixels);
void upipe_uyvy_to_planar_8_avx  (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, uintptr_t pixels);
void upipe_uyvy_to_planar_8_avx2 (uint8_t *y, uint8_t *u, uint8_t *v, const uint16_t *l, uintptr_t pixels);

void upipe_uyvy_to_planar_10_c(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, uintptr_t width);
/* process mmsize pixels per iteration */
void upipe_uyvy_to_planar_10_ssse3(uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, uintptr_t pixels);
void upipe_uyvy_to_planar_10_avx  (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, uintptr_t pixels);
void upipe_uyvy_to_planar_10_avx2 (uint16_t *y, uint16_t *u, uint16_t *v, const uint16_t *l, uintptr_t pixels);

void upipe_uyvy_to_v210_c(const uint16_t *y, uint8_t *dst, uintptr_t width);
/* process (mmsize*3)/8 pixels per iteration */
void upipe_uyvy_to_v210_ssse3(const uint16_t *y, uint8_t *dst, uintptr_t pixels);
void upipe_uyvy_to_v210_avx  (const uint16_t *y, uint8_t *dst, uintptr_t pixels);
void upipe_uyvy_to_v210_avx2 (const uint16_t *y, uint8_t *dst, uintptr_t pixels);

#ifndef _V210DEC_H_
/** @hidden */
#define _V210DEC_H_

void upipe_v210_to_planar_10_c(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);
void upipe_v210_to_planar_8_c(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_v210_to_planar_10_ssse3(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);
void upipe_v210_to_planar_10_avx  (const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);
void upipe_v210_to_planar_10_avx2 (const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels);

/* process (6*mmsize)/16 pixels per iteration */
void upipe_v210_to_planar_8_ssse3(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
void upipe_v210_to_planar_8_avx  (const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);
void upipe_v210_to_planar_8_avx2 (const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels);

#endif

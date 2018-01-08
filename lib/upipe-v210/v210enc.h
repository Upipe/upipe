#include <inttypes.h>

void upipe_planar_to_v210_8_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_planar_to_v210_10_c(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t width);

void upipe_planar_to_v210_10_avx2(const uint16_t *y, const uint16_t *u,
                                    const uint16_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_planar_to_v210_10_ssse3(const uint16_t *y, const uint16_t *u,
                                     const uint16_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_planar_to_v210_8_ssse3(const uint8_t *y, const uint8_t *u,
                                    const uint8_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_planar_to_v210_8_avx(const uint8_t *y, const uint8_t *u,
                                  const uint8_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_planar_to_v210_8_avx2(const uint8_t *y, const uint8_t *u,
                                   const uint8_t *v, uint8_t *dst, ptrdiff_t width);

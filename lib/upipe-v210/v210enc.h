#include <inttypes.h>

void upipe_v210enc_planar_pack_8_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_v210enc_planar_pack_10_c(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *dst, ptrdiff_t width);

void upipe_v210_planar_pack_10_avx2(const uint16_t *y, const uint16_t *u,
                                    const uint16_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_v210_planar_pack_10_ssse3(const uint16_t *y, const uint16_t *u,
                                     const uint16_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_v210_planar_pack_8_ssse3(const uint8_t *y, const uint8_t *u,
                                    const uint8_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_v210_planar_pack_8_avx(const uint8_t *y, const uint8_t *u,
                                  const uint8_t *v, uint8_t *dst, ptrdiff_t width);
void upipe_v210_planar_pack_8_avx2(const uint8_t *y, const uint8_t *u,
                                   const uint8_t *v, uint8_t *dst, ptrdiff_t width);

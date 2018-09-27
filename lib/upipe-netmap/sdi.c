#include <inttypes.h>
#include "sdi.h"

void upipe_planar_to_sdi_8_c(const uint8_t *y, const uint8_t *u, const uint8_t *v, uint8_t *l, const int64_t width)
{
    for (int j = 0; j < width/2; j++) {
        uint8_t u1 = *u++;
        uint8_t v1 = *v++;
        uint8_t y1 = *y++;
        uint8_t y2 = *y++;

        *l++ = u1;                                  // uuuuuuuu
        *l++ = y1 >> 2;                             // 00yyyyyy
        *l++ = (y1 & 0x3) << 6 | ((v1 >> 4) & 0xf); // yy00vvvv
        *l++ = (v1 << 4) | (y2 >> 6);               // vvvv00yy
        *l++ = y2 << 2;                             // yyyyyy00
    }
}

void upipe_planar_to_sdi_10_c(const uint16_t *y, const uint16_t *u, const uint16_t *v, uint8_t *l, const int64_t width)
{
    for (int j = 0; j < width/2; j++) {
        uint16_t u1 = *u++;
        uint16_t v1 = *v++;
        uint16_t y1 = *y++;
        uint16_t y2 = *y++;

        *l++ = (u1 >> 2) & 0xff;                        // uuuuuuuu
        *l++ = ((u1 & 0x3) << 6) | ((y1 >> 4) & 0x3f);  // uuyyyyyy
        *l++ = ((y1 & 0xf) << 4) | ((v1 >> 6) & 0xf);   // yyyyvvvv
        *l++ = ((v1 & 0xf) << 4) | ((y2 >> 8) & 0x3);   // vvvvvvyy
        *l++ = y2 & 0xff;                               // yyyyyyyy
    }
}

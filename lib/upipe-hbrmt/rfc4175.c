#include <inttypes.h>
#include "rfc4175_enc.h"

void upipe_v210_to_sdi_c(const uint32_t *src, uint8_t *dst, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i += 6) {
        uint16_t a, b, c, d;

        a = src[0] & 0x3ff;
        b = (src[0] >> 10) & 0x3ff;
        c = (src[0] >> 20) & 0x3ff;
        d = src[1] & 0x3ff;
        dst[0] = a >> 2;
        dst[1] = (a << 6) | (b >> 4);
        dst[2] = (b << 4) | (c >> 6);
        dst[3] = (c << 2) | (d >> 8);
        dst[4] = d;

        a = (src[1] >> 10) & 0x3ff;
        b = (src[1] >> 20) & 0x3ff;
        c = src[2] & 0x3ff;
        d = (src[2] >> 10) & 0x3ff;
        dst[5] = a >> 2;
        dst[6] = (a << 6) | (b >> 4);
        dst[7] = (b << 4) | (c >> 6);
        dst[8] = (c << 2) | (d >> 8);
        dst[9] = d;

        a = (src[2] >> 20) & 0x3ff;
        b = src[3] & 0x3ff;
        c = (src[3] >> 10) & 0x3ff;
        d = (src[3] >> 20) & 0x3ff;
        dst[10] = a >> 2;
        dst[11] = (a << 6) | (b >> 4);
        dst[12] = (b << 4) | (c >> 6);
        dst[13] = (c << 2) | (d >> 8);
        dst[14] = d;

        src += 4;
        dst += 15;
    }
}

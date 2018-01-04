#include <stddef.h>
#include <inttypes.h>
#include "sdidec.h"

void upipe_sdi_unpack_c(const uint8_t *src, uint16_t *y, uintptr_t pixels)
{
    for (int i = 0; i < 2*pixels; i += 4) {
        uint8_t a = *src++;
        uint8_t b = *src++;
        uint8_t c = *src++;
        uint8_t d = *src++;
        uint8_t e = *src++;
        y[i+0] = (a << 2)          | ((b >> 6) & 0x03); //1111111122
        y[i+1] = ((b & 0x3f) << 4) | ((c >> 4) & 0x0f); //2222223333
        y[i+2] = ((c & 0x0f) << 6) | ((d >> 2) & 0x3f); //3333444444
        y[i+3] = ((d & 0x03) << 8) | e;                 //4455555555
    }
}

void upipe_sdi_v210_unpack_c(const uint8_t *src, uint32_t *dst, uintptr_t pixels)
{
    while (pixels >= 6) {
        pixels -= 6;
        uint16_t a, b, c;

        a = ((src[0]  & 0xff) << 2) | (src[1] >> 6);
        b = ((src[1]  & 0x3f) << 4) | (src[2] >> 4);
        c = ((src[2]  & 0x0f) << 6) | (src[3] >> 2);
        *dst++ = (((c << 20) | (b << 10)) | a);

        a = ((src[3]  & 0x03) << 8) | (src[4]);
        b = ((src[5]  & 0xff) << 2) | (src[6] >> 6);
        c = ((src[6]  & 0x3f) << 4) | (src[7] >> 4);
        *dst++ = (((c << 20) | (b << 10)) | a);

        a = ((src[7]  & 0x0f) << 6) | (src[8]  >> 2);
        b = ((src[8]  & 0x03) << 8) | (src[9]);
        c = ((src[10] & 0xff) << 2) | (src[11] >> 6);
        *dst++ = (((c << 20) | (b << 10)) | a);

        a = ((src[11] & 0x3f) << 4) | (src[12] >> 4);
        b = ((src[12] & 0x0f) << 6) | (src[13] >> 2);
        c = ((src[13] & 0x03) << 8) | (src[14]);
        *dst++ = (((c << 20) | (b << 10)) | a);
        src += 15;
    }
}

void upipe_sdi_to_planar_8_c(const uint8_t *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i += 2) {
        uint8_t a = *src++; // UUUUUUUU
        uint8_t b = *src++; // ..YYYYYY
        uint8_t c = *src++; // YY..VVVV
        uint8_t d = *src++; // VVVV..YY
        uint8_t e = *src++; // YYYYYY..

		*u++ = a;
        *y++ = (b << 2) | (c >> 6);
        *v++ = (c << 4) | (d >> 4);
        *y++ = (d << 6) | (e >> 2);
    }
}

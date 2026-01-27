/*
 * 10 bit unpacking
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdint.h>
#include "sdidec.h"

void upipe_sdi_to_uyvy_c(const uint8_t *src, uint16_t *y, uintptr_t pixels)
{
    pixels *= 2; /* change to number of samples */
    for (int i = 0; i < pixels; i += 4) {
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

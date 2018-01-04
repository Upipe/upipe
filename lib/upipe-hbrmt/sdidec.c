/*
 * 10 bit unpacking
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#include <stdint.h>
#include "sdidec.h"

void upipe_sdi_unpack_c(const uint8_t *src, uint16_t *y, int64_t size)
{
    uint64_t pixels = size * 8 /10;

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

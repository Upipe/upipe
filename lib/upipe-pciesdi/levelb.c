/*
 * SDI-3G level B functions
 * Copyright (c) 2018 James Darnley <jdarnley@obe.tv>
 *
 * This file is part of Upipe
 *
 * Upipe is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Upipe is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Upipe; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <inttypes.h>
#include "levelb.h"

void upipe_levelb_to_uyvy_c(const uint8_t *src, uint16_t *dst1, uint16_t *dst2, uintptr_t pixels)
{
    for (int i = 0; i < pixels; i++) {
        uint8_t a = *src++;
        uint8_t b = *src++;
        uint8_t c = *src++;
        uint8_t d = *src++;
        uint8_t e = *src++;
        dst1[2*i+0] = (a << 2)          | (b >> 6); //1111111122
        dst2[2*i+0] = ((b & 0x3f) << 4) | (c >> 4); //2222223333
        dst1[2*i+1] = ((c & 0x0f) << 6) | (d >> 2); //3333444444
        dst2[2*i+1] = ((d & 0x03) << 8) | e;        //4455555555
    }
}

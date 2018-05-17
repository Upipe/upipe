/*
 * 10 bit packing
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

#include <upipe/ubits.h>

#include <arpa/inet.h>

#include <libavutil/bswap.h>

#include "sdienc.h"

#define CLIP8(c) (ubase_clip((*(c)), 1,  254))
#define CLIP(c)  (ubase_clip((*(c)), 4, 1019))

void upipe_planar_to_uyvy_8_c(uint16_t *dst, const uint8_t *y, const uint8_t *u, const uint8_t *v, const uintptr_t width)
{
    int j;
    for (j = 0; j < width/2; j++) {
        dst[0] = CLIP8(u++) << 2;
        dst[1] = CLIP8(y++) << 2;
        dst[2] = CLIP8(v++) << 2;
        dst[3] = CLIP8(y++) << 2;
        dst += 4;
    }
}

void upipe_planar_to_uyvy_10_c(uint16_t *dst, const uint16_t *y, const uint16_t *u, const uint16_t *v, const uintptr_t width)
{
    int j;
    for (j = 0; j < width/2; j++) {
        dst[0] = CLIP(u++);
        dst[1] = CLIP(y++);
        dst[2] = CLIP(v++);
        dst[3] = CLIP(y++);
        dst += 4;
    }
}

void upipe_uyvy_to_sdi_c(uint8_t *dst, const uint8_t *y, uintptr_t pixels)
{
    struct ubits s;
    uintptr_t size = pixels * 2; /* change to number of samples */
    ubits_init(&s, dst, size * 10 / 8);

    for (int i = 0; i < size; i ++)
        ubits_put(&s, 10, htons((y[2*i+0] << 8) | y[2*i+1]));

    uint8_t *end;
    if (!ubase_check(ubits_clean(&s, &end))) {
        // error
    } else {
        // check buffer end?
    }
}

void upipe_uyvy_to_sdi_2_c(uint8_t *dst1, uint8_t *dst2, const uint8_t *y, uintptr_t pixels)
{
    upipe_uyvy_to_sdi_c(dst1, y, pixels);
    memcpy(dst2, dst1, 2*pixels * 10 / 8);
}

#define READ_PIXELS(a, b, c)         \
    do {                             \
        val  = av_le2ne32(*src++);   \
        *a++ =  val & 0x3FF;         \
        *b++ = (val >> 10) & 0x3FF;  \
        *c++ = (val >> 20) & 0x3FF;  \
    } while (0)

void upipe_v210_to_uyvy_c(const uint32_t *src, uint16_t *uyvy, uintptr_t width)
{
    uint32_t val;
    int i;

    for( i = 0; i < width; i += 6 ){
        READ_PIXELS(uyvy, uyvy, uyvy);
        READ_PIXELS(uyvy, uyvy, uyvy);
        READ_PIXELS(uyvy, uyvy, uyvy);
        READ_PIXELS(uyvy, uyvy, uyvy);
    }
}

/*
 * V210 encoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 * Copyright (c) 2015 Open Broadcast Systems Ltd
 *
 * This file is based on the implementation in FFmpeg.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** @file
 * @short Upipe v210enc module
 */

#include <stdint.h>
#include <upipe-v210/upipe_v210enc.h>
#include "v210enc.h"

#define CLIP(v) ubase_clip(v, 4, 1019)
#define CLIP8(v) ubase_clip(v, 1, 254)

static inline void wl32(uint8_t *dst, uint32_t u)
{
    *dst++ = (u      ) & 0xff;
    *dst++ = (u >>  8) & 0xff;
    *dst++ = (u >> 16) & 0xff;
    *dst++ = (u >> 24) & 0xff;
}

#define WRITE_PIXELS(a, b, c)           \
    do {                                \
        val =   CLIP(*a++);             \
        val |= (CLIP(*b++) << 10) |     \
               (CLIP(*c++) << 20);      \
        wl32(dst, val);                 \
        dst += 4;                       \
    } while (0)

#define WRITE_PIXELS8(a, b, c)          \
    do {                                \
        val =  (CLIP8(*a++) << 2);      \
        val |= (CLIP8(*b++) << 12) |    \
               (CLIP8(*c++) << 22);     \
        wl32(dst, val);                 \
        dst += 4;                       \
    } while (0)

void upipe_planar_to_v210_8_c(const uint8_t *y, const uint8_t *u,
                                 const uint8_t *v, uint8_t *dst, ptrdiff_t width)
{
    uint32_t val;
    int i;

    /* unroll this to match the assembly */
    for( i = 0; i < width-11; i += 12 ){
        WRITE_PIXELS8(u, y, v);
        WRITE_PIXELS8(y, u, y);
        WRITE_PIXELS8(v, y, u);
        WRITE_PIXELS8(y, v, y);
        WRITE_PIXELS8(u, y, v);
        WRITE_PIXELS8(y, u, y);
        WRITE_PIXELS8(v, y, u);
        WRITE_PIXELS8(y, v, y);
    }
}

void upipe_planar_to_v210_10_c(const uint16_t *y, const uint16_t *u,
                                  const uint16_t *v, uint8_t *dst, ptrdiff_t width)
{
    uint32_t val;
    int i;

    for( i = 0; i < width-5; i += 6 ){
        WRITE_PIXELS(u, y, v);
        WRITE_PIXELS(y, u, y);
        WRITE_PIXELS(v, y, u);
        WRITE_PIXELS(y, v, y);
    }
}

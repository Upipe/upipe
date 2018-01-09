/*
 * V210 decoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 * Copyright (c) 2017 Open Broadcast Systems Ltd
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
 * @short Upipe v210dec module
 */

#include <stdint.h>

#include "v210dec.h"

// TODO: handle endianess

static inline uint32_t rl32(const void *src)
{
    const uint8_t *s = src;
    return s[0] |
        (s[1] <<  8) |
        (s[2] << 16) |
        (s[3] << 24);
}

#define READ_PIXELS_8(a, b, c) \
    do { \
        uint32_t val = rl32(src); \
        src += 4; \
        *(a)++ = (val >> 2)  & 255; \
        *(b)++ = (val >> 12) & 255; \
        *(c)++ = (val >> 22) & 255; \
    } while (0)

#define READ_PIXELS_10(a, b, c) \
    do { \
        uint32_t val = rl32(src); \
        src += 4; \
        *(a)++ = (val)       & 1023; \
        *(b)++ = (val >> 10) & 1023; \
        *(c)++ = (val >> 20) & 1023; \
    } while (0)

void upipe_v210_to_planar_8_c(const void *src, uint8_t *y, uint8_t *u, uint8_t *v, uintptr_t pixels)
{
    /* unroll this to match the assembly */
    for(int i = 0; i < pixels-5; i += 6 ){
        READ_PIXELS_8(u, y, v);
        READ_PIXELS_8(y, u, y);
        READ_PIXELS_8(v, y, u);
        READ_PIXELS_8(y, v, y);
    }
}

void upipe_v210_to_planar_10_c(const void *src, uint16_t *y, uint16_t *u, uint16_t *v, uintptr_t pixels)
{
    for(int i = 0; i < pixels-5; i += 6 ){
        READ_PIXELS_10(u, y, v);
        READ_PIXELS_10(y, u, y);
        READ_PIXELS_10(v, y, u);
        READ_PIXELS_10(y, v, y);
    }
}

/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * Authors: Christophe Massiot
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

/** @file
 * @short Upipe common utils for framers
 */

#include <stdint.h>

#include <upipe-framers/upipe_framers_common.h>

/** @This scans for an MPEG-style 3-octet start code in a linear buffer.
 *
 * @param p linear buffer
 * @param end end of linear buffer
 * @param state state of the algorithm
 * @return pointer to start code, or end if not found
 */
/* Code from libav/libavcodec/mpegvideo.c, published under LGPL 2.1+ */
const uint8_t *upipe_framers_mpeg_scan(const uint8_t *restrict p,
                                       const uint8_t *end,
                                       uint32_t *restrict state)
{
    int i;
    for (i = 0; i < 3; i++) {
        uint32_t tmp = *state << 8;
        *state = tmp + *(p++);
        if (tmp == 0x100 || p == end)
            return p;
    }

    while (p < end) {
        if      (p[-1] > 1      ) p += 3;
        else if (p[-2]          ) p += 2;
        else if (p[-3]|(p[-1]-1)) p++;
        else {
            p++;
            break;
        }
    }

    if (p > end)
        p = end;
    *state = ((uint32_t)p[-4] << 24) | (p[-3] << 16) | (p[-2] << 8) | p[-1];

    return p;
}
/* End code */


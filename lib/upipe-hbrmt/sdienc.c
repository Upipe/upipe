/*
 * 10 bit packing
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
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

#include <upipe/ubits.h>

#include <arpa/inet.h>

#include "sdienc.h"

void upipe_uyvy_to_sdi_c(uint8_t *dst, const uint8_t *y, uintptr_t size)
{
    struct ubits s;
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

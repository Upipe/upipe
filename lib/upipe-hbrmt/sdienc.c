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

#include "sdienc.h"

void upipe_uyvy_to_sdi_c(uint8_t *dst, const uint8_t *y, int64_t pixels)
{
    struct ubits s;
    int64_t size = pixels * 2; /* change to number of samples */
    ubits_init(&s, dst, size * 10 / 8);

    for (int i = 0; i < size; i ++)
        ubits_put(&s, 10, htons((y[2*i+0] << 8) | y[2*i+1]));
}

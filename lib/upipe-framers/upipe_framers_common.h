/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

#include <upipe/ubuf_block_stream.h>

/** @This scans for an MPEG-style 3-octet start code in a linear buffer.
 *
 * @param p linear buffer
 * @param end end of linear buffer
 * @param state state of the algorithm
 * @return pointer to start code, or end if not found
 */
const uint8_t *upipe_framers_mpeg_scan(const uint8_t *restrict p,
                                       const uint8_t *end,
                                       uint32_t *restrict state);

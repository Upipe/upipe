/*
 * Copyright (C) 2013-2016 OpenHeadend S.A.R.L.
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
 * @short Common framer functions for H.26x
 */

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block_stream.h>

#include "upipe_h26x_common.h"

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

/** @This translates the h26x aspect_ratio_idc to urational */
const struct urational upipe_h26xf_sar_from_idc[17] = {
    { .num = 1, .den = 1 }, /* unspecified - treat as square */
    { .num = 1, .den = 1 },
    { .num = 12, .den = 11 },
    { .num = 10, .den = 11 },
    { .num = 16, .den = 11 },
    { .num = 40, .den = 33 },
    { .num = 24, .den = 11 },
    { .num = 20, .den = 11 },
    { .num = 32, .den = 11 },
    { .num = 80, .den = 33 },
    { .num = 18, .den = 11 },
    { .num = 15, .den = 11 },
    { .num = 64, .den = 33 },
    { .num = 160, .den = 99 },
    { .num = 4, .den = 3 },
    { .num = 3, .den = 2 },
    { .num = 2, .den = 1 }
};

/** @This initializes the helper structure for octet stream.
 *
 * @param f helper structure
 */
void upipe_h26xf_stream_init(struct upipe_h26xf_stream *f)
{
    f->zeros = 0;
}

/** @This gets the next octet in the ubuf while bypassing escape words.
 *
 * @param s helper structure
 * @param ubuf pointer to block ubuf
 * @param octet_p reference to returned value
 * @return an error code
 */
int upipe_h26xf_stream_get(struct ubuf_block_stream *s, uint8_t *octet_p)
{
    UBASE_RETURN(ubuf_block_stream_get(s, octet_p))
    struct upipe_h26xf_stream *f =
        container_of(s, struct upipe_h26xf_stream, s);
    f->zeros <<= 1;
    if (unlikely(!*octet_p))
        f->zeros |= 1;
    else if (unlikely(*octet_p == 3 && (f->zeros & 6) == 6)) /* escape word */
        return upipe_h26xf_stream_get(s, octet_p);
    return UBASE_ERR_NONE;
}

/** @This reads an unsigned exp-golomb code from a stream.
 *
 * @param s ubuf block stream
 * @return code read
 */
uint32_t upipe_h26xf_stream_ue(struct ubuf_block_stream *s)
{
    int i = 1;
    while (i < 32) {
        upipe_h26xf_stream_fill_bits(s, 8);
        uint8_t octet = ubuf_block_stream_show_bits(s, 8);
        if (likely(octet))
            break;
        i += 8;
        ubuf_block_stream_skip_bits(s, 8);
    }
    while (i < 32 && !ubuf_block_stream_show_bits(s, 1)) {
        i++;
        ubuf_block_stream_skip_bits(s, 1);
    }

    if (likely(i <= 24)) {
        upipe_h26xf_stream_fill_bits(s, i);
        uint32_t result = ubuf_block_stream_show_bits(s, i);
        ubuf_block_stream_skip_bits(s, i);
        return result - 1;
    }

    upipe_h26xf_stream_fill_bits(s, 8);
    uint32_t result = ubuf_block_stream_show_bits(s, 8);
    ubuf_block_stream_skip_bits(s, 8);
    i -= 8;
    result <<= i;
    upipe_h26xf_stream_fill_bits(s, i);
    result += ubuf_block_stream_show_bits(s, i);
    ubuf_block_stream_skip_bits(s, i);
    return result - 1;
}

/** @This reads a signed exp-golomb code from a stream.
 *
 * @param s ubuf block stream
 * @return code read
 */
int32_t upipe_h26xf_stream_se(struct ubuf_block_stream *s)
{
    uint32_t v = upipe_h26xf_stream_ue(s);

    return (v & 1) ? (v + 1) / 2 : -(v / 2);
}

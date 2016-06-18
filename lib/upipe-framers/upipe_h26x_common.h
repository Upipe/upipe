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

#include <stdlib.h>
#include <stdint.h>

/** @This translates the h26x aspect_ratio_idc to urational */
extern const struct urational upipe_h26xf_sar_from_idc[17];

/** @This allows to skip escape words from NAL units. */
struct upipe_h26xf_stream {
    /** positions of the 0s in the previous octets */
    uint8_t zeros;
    /** standard octet stream */
    struct ubuf_block_stream s;
};

/** @internal @This initializes the helper structure for octet stream.
 *
 * @param f helper structure
 */
void upipe_h26xf_stream_init(struct upipe_h26xf_stream *f);

/** @This gets the next octet in the ubuf while bypassing escape words.
 *
 * @param s helper structure
 * @param ubuf pointer to block ubuf
 * @param octet_p reference to returned value
 * @return an error code
 */
int upipe_h26xf_stream_get(struct ubuf_block_stream *s, uint8_t *octet_p);

/** @This fills the bit stream cache with at least the given number of bits.
 *
 * @param s helper structure
 * @param nb number of bits to ensure
 */
#define upipe_h26xf_stream_fill_bits(s, nb)                                 \
    ubuf_block_stream_fill_bits_inner(s, upipe_h26xf_stream_get, nb)

/** @internal @This reads an unsigned exp-golomb code from a stream.
 *
 * @param s ubuf block stream
 * @return code read
 */
uint32_t upipe_h26xf_stream_ue(struct ubuf_block_stream *s);

/** @internal @This reads a signed exp-golomb code from a stream.
 *
 * @param s ubuf block stream
 * @return code read
 */
int32_t upipe_h26xf_stream_se(struct ubuf_block_stream *s);

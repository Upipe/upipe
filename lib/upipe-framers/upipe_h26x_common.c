/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
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
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_stream.h>
#include <upipe/uref_block.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-framers/uref_h26x.h>

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

/** @This allocates a ubuf containing an annex B header.
 *
 * @param ubuf_mgr pointer to ubuf manager
 * @return pointer to ubuf containing an annex B header
 */
struct ubuf *upipe_h26xf_alloc_annexb(struct ubuf_mgr *ubuf_mgr)
{
    uint8_t header[4];
    header[0] = 0;
    header[1] = 0;
    header[2] = 0;
    header[3] = 1;
    return ubuf_block_alloc_from_opaque(ubuf_mgr, header, 4);
}

/** @internal @This decapsulates NAL unit.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @param nal_offset offset of the NAL unit
 * @param nal_size_p size of the NAL unit, including start code (modified)
 * @param encaps_input input H26x encapsulation
 * @param nal_offset_correction_p reference to the correction to the offsets of
 * the next NALs
 * @return an error code
 */
static int upipe_h26xf_decaps_nal(struct uref *uref,
        uint64_t nal_offset, uint64_t *nal_size_p,
        enum uref_h26x_encaps encaps_input, int64_t *nal_offset_correction_p)
{
    unsigned int encaps_size;

    switch (encaps_input) {
        case UREF_H26X_ENCAPS_NALU:
            return UBASE_ERR_NONE;

        case UREF_H26X_ENCAPS_ANNEXB: {
            uint8_t startcode[3];
            UBASE_RETURN(uref_block_extract(uref, nal_offset, 3, startcode))

            assert(!startcode[0] && !startcode[1]);
            encaps_size = startcode[2] == 1 ? 3 : 4;
            break;
        }

        case UREF_H26X_ENCAPS_LENGTH1:
            encaps_size = 1;
            break;
        case UREF_H26X_ENCAPS_LENGTH2:
            encaps_size = 2;
            break;
        default:
        case UREF_H26X_ENCAPS_LENGTH4:
            encaps_size = 4;
            break;
    }

    UBASE_RETURN(uref_block_delete(uref, nal_offset, encaps_size))
    *nal_size_p -= encaps_size;
    *nal_offset_correction_p -= encaps_size;
    return UBASE_ERR_NONE;
}

/** @internal @This encapsulates NAL unit.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @param nal_offset offset of the NAL unit
 * @param nal_size_p size of the NAL unit, including start code (modified)
 * @param encaps_output output H26x encapsulation
 * @param ubuf_mgr ubuf manager
 * @param annexb_header pointer to ubuf containing an annex B startcode
 * @param nal_offset_correction_p reference to the correction to the offsets of
 * the next NALs
 * @return an error code
 */
static int upipe_h26xf_encaps_nal(struct uref *uref,
        uint64_t nal_offset, uint64_t *nal_size_p,
        enum uref_h26x_encaps encaps_output,
        struct ubuf_mgr *ubuf_mgr, struct ubuf *annexb_header,
        int64_t *nal_offset_correction_p)
{
    struct ubuf *insert;
    uint64_t nal_size = *nal_size_p;

    switch (encaps_output) {
        case UREF_H26X_ENCAPS_NALU:
            return UBASE_ERR_NONE;

        case UREF_H26X_ENCAPS_ANNEXB:
            insert = ubuf_dup(annexb_header);
            break;

        case UREF_H26X_ENCAPS_LENGTH1: {
            uint8_t length_buf[1];
            if (nal_size > UINT8_MAX)
                return UBASE_ERR_INVALID;
            length_buf[0] = nal_size & 0xff;
            insert = ubuf_block_alloc_from_opaque(ubuf_mgr, length_buf, 1);
            break;
        }

        case UREF_H26X_ENCAPS_LENGTH2: {
            uint8_t length_buf[2];
            if (nal_size > UINT16_MAX)
                return UBASE_ERR_INVALID;
            length_buf[0] = (nal_size >> 8) & 0xff;
            length_buf[1] = nal_size & 0xff;
            insert = ubuf_block_alloc_from_opaque(ubuf_mgr, length_buf, 2);
            break;
        }

        default:
        case UREF_H26X_ENCAPS_LENGTH4: {
            uint8_t length_buf[4];
            if (nal_size > UINT32_MAX)
                return UBASE_ERR_INVALID;
            length_buf[0] = nal_size >> 24;
            length_buf[1] = (nal_size >> 16) & 0xff;
            length_buf[2] = (nal_size >> 8) & 0xff;
            length_buf[3] = nal_size & 0xff;
            insert = ubuf_block_alloc_from_opaque(ubuf_mgr, length_buf, 4);
            break;
        }
    }

    size_t encaps_size;
    if (unlikely(insert == NULL ||
                 !ubase_check(ubuf_block_size(insert, &encaps_size))))
        return UBASE_ERR_ALLOC;

    UBASE_RETURN(uref_block_insert(uref, nal_offset, insert))
    *nal_size_p += encaps_size;
    *nal_offset_correction_p += encaps_size;
    return UBASE_ERR_NONE;
}

/** @This converts a frame from an encapsulation to another.
 *
 * @param upipe description structure of the pipe
 * @param uref pointer to uref
 * @param encaps_input input H26x encapsulation
 * @param encaps_output output H26x encapsulation
 * @param ubuf_mgr ubuf manager
 * @param annexb_header pointer to ubuf containing an annex B startcode
 * @return an error code
 */
int upipe_h26xf_convert_frame(struct uref *uref,
        enum uref_h26x_encaps encaps_input, enum uref_h26x_encaps encaps_output,
        struct ubuf_mgr *ubuf_mgr, struct ubuf *annexb_header)
{
    if (encaps_output == encaps_input)
        return UBASE_ERR_NONE;
    uint64_t vcl_offset = 0;
    uref_block_get_header_size(uref, &vcl_offset);

    uint64_t nal_units = 0;
    uint64_t nal_offset = 0;
    uint64_t nal_size = 0;
    int64_t nal_offset_correction = 0;
    while (ubase_check(uref_h26x_iterate_nal(uref, &nal_units,
                                             &nal_offset, &nal_size,
                                             nal_offset_correction))) {
        UBASE_RETURN(upipe_h26xf_decaps_nal(uref, nal_offset, &nal_size,
                    encaps_input, &nal_offset_correction))
        UBASE_RETURN(upipe_h26xf_encaps_nal(uref, nal_offset, &nal_size,
                    encaps_output, ubuf_mgr, annexb_header,
                    &nal_offset_correction))

        if (vcl_offset && vcl_offset <= nal_offset + nal_size) {
            uref_block_set_header_size(uref,
                                       vcl_offset + nal_offset_correction);
            vcl_offset = 0;
        }
    }


    return UBASE_ERR_NONE;
}


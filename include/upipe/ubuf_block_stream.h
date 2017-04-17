/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe streams of block buffers
 */

#ifndef _UPIPE_UBUF_BLOCK_STREAM_H_
/** @hidden */
#define _UPIPE_UBUF_BLOCK_STREAM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/** @This is a helper allowing to read streams from a block ubuf. */
struct ubuf_block_stream {
    /** pointer to ubuf */
    struct ubuf *ubuf;
    /** next buffer position */
    const uint8_t *buffer;
    /** end of block section */
    const uint8_t *end;
    /** offset of the block section */
    int offset;
    /** size of the block section */
    int size;

    /** bits cache */
    uint32_t bits;
    /** number of cached bits */
    uint32_t available;
    /** true if the bit stream cache overflows */
    bool overflow;
};

/** @This initializes the helper structure for octet stream using a ubuf.
 *
 * @param s helper structure
 * @param ubuf pointer to block ubuf
 * @param offset start offset in octets
 * @return an error code
 */
static inline int ubuf_block_stream_init(struct ubuf_block_stream *s,
                                         struct ubuf *ubuf, int offset)
{
    s->size = -1;
    UBASE_RETURN(ubuf_block_read(ubuf, offset, &s->size, &s->buffer))
    s->ubuf = ubuf;
    s->offset = offset;
    s->end = s->buffer + s->size;
    s->bits = 0;
    s->available = 0;
    s->overflow = false;
    return UBASE_ERR_NONE;
}

/** @This initializes the helper structure for octet stream using an opaque.
 * Note that the opaque must remain allocated until @ref ubuf_block_stream_clean
 * is called.
 *
 * @param s helper structure
 * @param buffer pointer to opaque buffer
 * @param size size of the opaque buffer
 */
static inline void
    ubuf_block_stream_init_from_opaque(struct ubuf_block_stream *s,
                                       const uint8_t *buffer, size_t size)
{
    s->size = size;
    s->buffer = buffer;
    s->ubuf = NULL;
    s->offset = 0;
    s->end = s->buffer + s->size;
    s->bits = 0;
    s->available = 0;
    s->overflow = false;
}

/** @This cleans up the helper structure for octet stream.
 *
 * @param s helper structure
 * @return an error code
 */
static inline int
    ubuf_block_stream_clean(struct ubuf_block_stream *s)
{
    if (s->ubuf != NULL)
        UBASE_RETURN(ubuf_block_unmap(s->ubuf, s->offset));
    return UBASE_ERR_NONE;
}

/** @This computes the position (in bits) since the beginning of the ubuf.
 *
 * @param s helper structure
 * @return position in bits
 */
static inline int ubuf_block_stream_position(struct ubuf_block_stream *s)
{
    return (s->offset + s->size - (s->end - s->buffer)) * 8 - s->available;
}

/** @This gets the next octet in the ubuf.
 *
 * @param s helper structure
 * @param octet_t filled in with the read octet
 * @return an error code
 */
static inline int ubuf_block_stream_get(struct ubuf_block_stream *s,
                                        uint8_t *octet_p)
{
    if (unlikely(s->buffer >= s->end)) {
        if (s->ubuf == NULL)
            return UBASE_ERR_INVALID;
        ubuf_block_unmap(s->ubuf, s->offset);
        s->offset += s->size;
        s->size = -1;
        if (unlikely(!ubase_check(ubuf_block_read(s->ubuf, s->offset,
                                                  &s->size, &s->buffer)))) {
            s->ubuf = NULL;
            return UBASE_ERR_INVALID;
        }
        s->end = s->buffer + s->size;
    }
    *octet_p = *s->buffer++;
    return UBASE_ERR_NONE;
}

/** @This fills the bit stream cache with at least the given number of bits,
 * with a custom function to pop octets.
 *
 * @param s helper structure
 * @param get_octet function to get extra octets
 * @param nb number of bits to ensure
 */
#define ubuf_block_stream_fill_bits_inner(s, get_octet, nb)                 \
    while ((s)->available < (nb)) {                                         \
        uint8_t octet;                                                      \
        if (unlikely(!ubase_check(get_octet((s), &octet)))) {               \
            octet = 0;                                                      \
            (s)->overflow = true;                                           \
        }                                                                   \
        (s)->bits += (uint32_t)octet << (24 - (s)->available);              \
        (s)->available += 8;                                                \
        assert((s)->available <= 32);                                       \
    }

/** @This fills the bit stream cache with at least the given number of bits.
 *
 * @param s helper structure
 * @param nb number of bits to ensure
 */
#define ubuf_block_stream_fill_bits(s, nb)                                  \
    ubuf_block_stream_fill_bits_inner(s, ubuf_block_stream_get, nb)

/** @This returns the given number of bits from the cache.
 *
 * @param s helper structure
 * @param nb number of bits to return
 * @return bits from the cache
 */
#define ubuf_block_stream_show_bits(s, nb)                                  \
    ((s)->bits >> (32 - (nb)))

/** @This discards the given number of bits from the cache.
 *
 * @param s helper structure
 * @param nb number of bits to discard
 */
#define ubuf_block_stream_skip_bits(s, nb)                                  \
    do {                                                                    \
        assert((nb) <= (s)->available);                                     \
        (s)->bits <<= (nb);                                                 \
        (s)->available -= (nb);                                             \
    } while (0)

/** @This initializes the helper structure for octet stream using a ubuf,
 * with an offset in bits
 *
 * @param s helper structure
 * @param ubuf pointer to block ubuf
 * @param offset start offset in bits
 * @return an error code
 */
static inline int ubuf_block_stream_init_bits(struct ubuf_block_stream *s,
                                              struct ubuf *ubuf, int offset)
{
    UBASE_RETURN(ubuf_block_stream_init(s, ubuf, offset / 8));
    int bits = offset % 8;
    if (bits) {
        ubuf_block_stream_fill_bits(s, bits);
        ubuf_block_stream_skip_bits(s, bits);
    }
    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

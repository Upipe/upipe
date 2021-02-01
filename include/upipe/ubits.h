/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short Upipe bit-oriented writer and reader
 */

#ifndef _UPIPE_UBITS_H_
/** @hidden */
#define _UPIPE_UBITS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/** @This is a helper allowing to write bits by bits to a buffer. */
struct ubits {
    /** pointer to buffer */
    uint8_t *buffer;
    /** end of buffer */
    uint8_t *buffer_end;

    /** bits cache */
    uint32_t bits;
    /** number of available bits */
    uint32_t available;
    /** true if the bit stream cache overflows */
    bool overflow;
};

enum ubits_direction {
    UBITS_WRITE,
    UBITS_READ
};

/** @This initializes the helper structure for bit-oriented writer.
 *
 * @param s helper structure
 * @param buffer pointer to buffer
 * @param buffer_size buffer size in octets
 * @param dir direction (read or write)
 */
static inline void ubits_init(struct ubits *s,
                              uint8_t *buffer, size_t buffer_size, enum ubits_direction dir)
{
    s->buffer = buffer;
    s->buffer_end = buffer + buffer_size;
    s->bits = 0;
    s->available = (dir == UBITS_READ) ? 0 : 32;
    s->overflow = false;
}

/** @This returns up to 32 bits read from the bitstream.
 *
 * @param s helper structure
 * @param nb number of bits to read
 */
static inline uint32_t ubits_get(struct ubits *s, uint8_t nb)
{
    assert(nb && nb <= 32);

    if (s->available == 0) {
        if (unlikely(s->buffer == s->buffer_end)) {
            s->overflow = true;
            return 0;
        }
        s->bits = *s->buffer++;
        s->available = 8;
    }

    if (nb <= s->available) {
        s->available -= nb;
        return (s->bits >> s->available) & ((1 << nb) - 1);
    }

    nb -= s->available;
    /* Mask out bits which are not available */
    uint32_t val = (s->bits & ((1 << s->available) - 1)) << nb;

    if (unlikely(s->buffer + (nb + 7) / 8 > s->buffer_end)) {
        s->overflow = true;
        s->available = 0;
        return 0;
    }

    while (nb >= 8) {
        val |= *s->buffer++ << (nb - 8);
        nb -= 8;
    }

    if (nb) {
        /* Reload buffer and use the bits we want */
        s->available = 8 - nb;
        s->bits = *s->buffer++;
        val |= s->bits >> s->available;
    }

    return val;
}

/** @This puts up to 32 bits into the bitstream.
 *
 * @param s helper structure
 * @param nb number of bits to write
 * @param value value to write
 */
static inline void ubits_put(struct ubits *s, uint8_t nb, uint32_t value)
{
    assert(nb && nb <= 32);
    assert(nb == 32 || value < (1U << nb));

    if (nb < s->available) {
        s->bits = (s->bits << nb) | value;
        s->available -= nb;
        return;
    }

    if (unlikely(s->buffer + 4 > s->buffer_end)) {
        s->overflow = true;
        return;
    }

    s->bits <<= s->available;
    s->bits |= value >> (nb - s->available);
    *s->buffer++ = s->bits >> 24;
    *s->buffer++ = (s->bits & 0xffffff) >> 16;
    *s->buffer++ = (s->bits & 0xffff) >> 8;
    *s->buffer++ = s->bits & 0xff;
    s->bits = value;
    s->available += 32 - nb;
}

/** @This cleans up the helper structure for bit-oriented writer.
 *
 * @param s helper structure
 * @param buffer_end_p filled in with a pointer to the first non-written octet
 * @return an error code
 */
static inline int ubits_clean(struct ubits *s, uint8_t **buffer_end_p)
{
    if (unlikely(s->overflow))
        return UBASE_ERR_NOSPC;

    if (s->available < 32)
        s->bits <<= s->available;
    while (s->available < 32) {
        if (unlikely(s->buffer + 1 > s->buffer_end))
            return UBASE_ERR_NOSPC;

        *s->buffer++ = s->bits >> 24;
        s->bits <<= 8;
        s->available += 8;
    }

    *buffer_end_p = s->buffer;
    return UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif

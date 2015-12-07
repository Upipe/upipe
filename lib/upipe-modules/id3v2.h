/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

/*
 * ID3 tag version 2
 */
#ifndef _UPIPE_MODULES_ID3V2_H_
# define _UPIPE_MODULES_ID3V2_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Overall tag structure:
 *      +-----------------------------+
 *      |      Header (10 bytes)      |
 *      +-----------------------------+
 *      |       Extended Header       |
 *      | (variable length, OPTIONAL) |
 *      +-----------------------------+
 *      |   Frames (variable length)  |
 *      +-----------------------------+
 *      |           Padding           |
 *      | (variable length, OPTIONAL) |
 *      +-----------------------------+
 *      | Footer (10 bytes, OPTIONAL) |
 *      +-----------------------------+
 *
 * Header:
 *      +-----------------------------+---------+
 *      | File identifier "ID3"       | 3 bytes |
 *      +-----------------------------+---------+
 *      | Verion                      | 2 bytes |
 *      |+----------------------------+--------+|
 *      || First byte: version        | 1 byte ||
 *      || Second byte: revision      | 1 byte ||
 *      |+----------------------------+--------+|
 *      +-----------------------------+---------+
 *      | Flags                       | 1 byte  |
 *      |+----------------------------+--------+|
 *      || Unsynchronisation          | 1 bit  ||
 *      || Extended header            | 1 bit  ||
 *      || Experimental               | 1 bit  ||
 *      || Footer                     | 1 bit  ||
 *      || reserved (0)               | 4 bit  ||
 *      |+----------------------------+--------+|
 *      +-----------------------------+---------+
 *      | Size                        | 4 bytes |
 *      +-----------------------------+---------+
 *
 */

#define ID3V2_HEADER_SIZE       10
#define ID3V2_FOOTER_SIZE       10
#define ID3V2_FRAME_HEADER_SIZE 10

static inline bool id3v2_check_tag(const uint8_t *p_id3v2)
{
    return p_id3v2[0] == 'I' && p_id3v2[1] == 'D' && p_id3v2[2] == '3';
}

static inline uint8_t id3v2_get_version_major(const uint8_t *p_id3v2)
{
    return p_id3v2[3];
}

static inline uint8_t id3v2_get_version_rev(const uint8_t *p_id3v2)
{
    return p_id3v2[4];
}

#define ID3V2_UNSYNCHRONISATION   (1 << 7)
#define ID3V2_EXTENTED_HEADER     (1 << 6)
#define ID3V2_EXPERIMENTAL        (1 << 5)
#define ID3V2_FOOTER              (1 << 4)

static inline bool id3v2_check_flag(const uint8_t *p_id3v2, uint8_t flag)
{
    return !!(p_id3v2[5] & flag);
}

static inline bool id3v2_check_unsynchronisation(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_UNSYNCHRONISATION);
}

static inline bool id3v2_check_extented_header(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_EXTENTED_HEADER);
}

static inline bool id3v2_check_experimental(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_EXPERIMENTAL);
}

static inline bool id3v2_check_footer(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_FOOTER);
}

static inline uint32_t id3v2_unsynchsafe(const uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++)
        v += p[i] << ((3 - i) * 7);
    return v;
}

static inline uint32_t id3v2_get_size(const uint8_t *p_id3v2)
{
    return id3v2_unsynchsafe(p_id3v2 + 6);
}

static inline uint32_t id3v2_footer_get_size(const uint8_t *p_id3v2)
{
    return id3v2_check_footer(p_id3v2) ?  ID3V2_FOOTER_SIZE : 0;
}

static inline uint32_t id3v2_get_total_size(const uint8_t *p_id3v2)
{
    return id3v2_get_size(p_id3v2) +
           id3v2_footer_get_size(p_id3v2) +
           ID3V2_HEADER_SIZE;
}

static inline uint32_t id3v2_get_extented_header_size(const uint8_t *p_id3v2)
{
    if (!id3v2_check_extented_header(p_id3v2))
        return 0;
    return id3v2_unsynchsafe(p_id3v2 + ID3V2_HEADER_SIZE);
}

struct id3v2_frame {
    char id[4];
    uint32_t size;
    uint8_t flags[2];
    const uint8_t *data;
};

static inline bool id3v2_get_frame(const uint8_t *p_id3v2,
                                   struct id3v2_frame *frame)
{
    const uint8_t *p;

    if (frame->data == NULL)
        p = p_id3v2 + ID3V2_HEADER_SIZE +
            id3v2_get_extented_header_size(p_id3v2);
    else
        p = frame->data + frame->size;

    if (p >= p_id3v2 + ID3V2_HEADER_SIZE + id3v2_get_size(p_id3v2) ||
        p[0] == 0)
        return false;

    for (uint8_t i = 0; i < 4; i++)
        frame->id[i] = p[i];
    frame->size = id3v2_unsynchsafe(p + 4);
    for (uint8_t i = 0; i < 2; i++)
        frame->flags[i] = p[8 + i];
    frame->data = p + ID3V2_FRAME_HEADER_SIZE;
    return true;
}

static inline bool id3v2_frame_check_id(const struct id3v2_frame *frame,
                                        const char *id)
{
    for (uint8_t i = 0; i < 4; i++)
        if (frame->id[i] != id[i])
            return false;
    return true;
}

struct id3v2_frame_priv {
    const char *owner;
    uint32_t size;
    const uint8_t *data;
};

static inline bool id3v2_get_frame_priv(const struct id3v2_frame *frame,
                                        struct id3v2_frame_priv *priv)
{
    if (!id3v2_frame_check_id(frame, "PRIV"))
        return false;
    priv->owner = (const char *)frame->data;
    priv->size = frame->size;
    priv->data = frame->data;
    while (priv->size && *priv->data != 0) {
        priv->size--;
        priv->data++;
    }
    if (priv->size == 0)
        return false;
    priv->size--;
    priv->data++;
    return true;
}

#endif /* !_UPIPE_MODULES_ID3V2_H_ */

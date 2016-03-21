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

/** @file
 *
 * from @url http://id3.org/id3v2.4.0-structure
 *
 * @section {ID3 overview}
 * structure:
 * @code R
 * +-----------------------------+
 * |      Header (10 bytes)      |
 * +-----------------------------+
 * |       Extended Header       |
 * | (variable length, OPTIONAL) |
 * +-----------------------------+
 * |   Frames (variable length)  |
 * +-----------------------------+
 * |           Padding           |
 * | (variable length, OPTIONAL) |
 * +-----------------------------+
 * | Footer (10 bytes, OPTIONAL) |
 * +-----------------------------+
 * @end code
 * @end section
 *
 * @section {ID3 header}
 * structure:
 * @code R
 * +-----------------------------+---------+
 * | File identifier "ID3"       | 3 bytes |
 * +-----------------------------+---------+
 * | Verion                      | 2 bytes |
 * |+----------------------------+--------+|
 * || First byte: version        | 1 byte ||
 * || Second byte: revision      | 1 byte ||
 * |+----------------------------+--------+|
 * +-----------------------------+---------+
 * | Flags                       | 1 byte  |
 * |+----------------------------+--------+|
 * || Unsynchronisation          | 1 bit  ||
 * || Extended header            | 1 bit  ||
 * || Experimental               | 1 bit  ||
 * || Footer                     | 1 bit  ||
 * || reserved (0)               | 4 bit  ||
 * |+----------------------------+--------+|
 * +-----------------------------+---------+
 * | Size                        | 4 bytes |
 * +-----------------------------+---------+
 * @end code
 * The first 3 bytes @strong must be "ID3".
 *
 * The defined flags are:
 * @list
 * @item Unsynchronisation:
 *   Bit 7 in the indicates whether or not
 *   unsynchronisation is applied on all frames; a set bit indicates usage.
 * @item Extended header:
 *   Bit 6 indicates whether or not the header is
 *   followed by an extended header. A set bit indicates the presence of an
 *   extended header.
 * @item Experimental:
 *   Bit 5 is used as an 'experimental indicator'. This
 *   flag SHALL always be set when the tag is in an experimental stage.
 * @item Footer present:
 *   Bit 4 indicates that a footer is present at the very
 *   end of the tag. A set bit indicates the presence of a footer.
 * @end list
 *
 * @end section
 *
 * @section {ID3 frame}
 * structure:
 * @code R
 * +-----------------------------+---------+
 * | Frame ID                    | 4 bytes |
 * +-----------------------------+---------+
 * | Size                        | 4 bytes |
 * +-----------------------------+---------+
 * | Flags                       | 2 byte  |
 * +-----------------------------+---------+
 * @end code
 * @end section
 *
 * @section {Syncsafe integer}
 *
 * In some parts of the tag it is inconvenient to use the
 * unsychronisation scheme because the size of unsynchronised data is
 * not known in advance, which is particularly problematic with size
 * descriptors. The solution in ID3v2 is to use synchsafe integers, in
 * which there can never be any false synchs. Synchsafe integers are
 * integers that keep its highest bit (bit 7) zeroed, making seven bits
 * out of eight available. Thus a 32 bit synchsafe integer can store 28
 * bits of information.
 *
 * Example encoding 255 as a 16 bit syncsafe integer:
 * @code R
 * 255 (0000 0000 1111 1111) -> 383 (0000 0001 0111 1111)
 * @end code
 *
 * @end section
 *
 */

#ifndef _UPIPE_MODULES_ID3V2_H_
# define _UPIPE_MODULES_ID3V2_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/** @This is the id3 header size in bytes. */
#define ID3V2_HEADER_SIZE       10

/** @This is the id3 footer size in bytes. */
#define ID3V2_FOOTER_SIZE       10

/** @This is the id3 frame header size in bytes. */
#define ID3V2_FRAME_HEADER_SIZE 10

/** @This checks the presence of the id3 tag "ID3".
 * @xsee {ID3 header}
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return true if the tag is valid, false otherwise
 */
static inline bool id3v2_check_tag(const uint8_t *p_id3v2)
{
    return p_id3v2[0] == 'I' && p_id3v2[1] == 'D' && p_id3v2[2] == '3';
}

/** @This gets the version major of the id3 tag.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the version major
 */
static inline uint8_t id3v2_get_version_major(const uint8_t *p_id3v2)
{
    return p_id3v2[3];
}

/** @This gets the version revision of the id3 tag.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the version revision
 */
static inline uint8_t id3v2_get_version_rev(const uint8_t *p_id3v2)
{
    return p_id3v2[4];
}

/** @This is the unsynchronisation flag. */
#define ID3V2_UNSYNCHRONISATION   (1 << 7)
/** @This is the extented header flag. */
#define ID3V2_EXTENTED_HEADER     (1 << 6)
/** @This is the experimental flag. */
#define ID3V2_EXPERIMENTAL        (1 << 5)
/** @This is the footer flag. */
#define ID3V2_FOOTER              (1 << 4)

/** @This checks the presence of a flag.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @param flag flag to test
 * @return true if the flag is set, false otherwise
 */
static inline bool id3v2_check_flag(const uint8_t *p_id3v2, uint8_t flag)
{
    return !!(p_id3v2[5] & flag);
}

/** @This checks the presence of the unsynchronisation flag.
 * @This is equivalent to:
 * @code id3v2_check_flag(p_id3v2, ID3V2_UNSYNCHRONISATION); @end code
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return true if the flag is set, false otherwise
 */
static inline bool id3v2_check_unsynchronisation(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_UNSYNCHRONISATION);
}

/** @This checks the presence of the extented header flag.
 * @This is equivalent to:
 * @code id3v2_check_flag(p_id3v2, ID3V2_EXTENTED_HEADER); @end code
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return true if the flag is set, false otherwise
 */
static inline bool id3v2_check_extented_header(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_EXTENTED_HEADER);
}

/** @This checks the presence of the experimental flag.
 * @This is equivalent to:
 * @code id3v2_check_flag(p_id3v2, ID3V2_EXPERIMENTAL); @end code
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return true if the flag is set, false otherwise
 */
static inline bool id3v2_check_experimental(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_EXPERIMENTAL);
}

/** @This checks the presence of the footer flag.
 * @This is equivalent to:
 * @code id3v2_check_flag(p_id3v2, ID3V2_FOOTER); @end code
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return true if the flag is set, false otherwise
 */
static inline bool id3v2_check_footer(const uint8_t *p_id3v2)
{
    return id3v2_check_flag(p_id3v2, ID3V2_FOOTER);
}

/** @This unsynchronises a value.
 * @xsee {Syncsafe integer}
 *
 * @param p pointer to the synchronise value
 * @return the unsynchronise value
 */
static inline uint32_t id3v2_unsynchsafe(const uint8_t *p)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; i++)
        v += p[i] << ((3 - i) * 7);
    return v;
}

/** @This gets the id3 body size.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the size in bytes of the body
 */
static inline uint32_t id3v2_get_size(const uint8_t *p_id3v2)
{
    return id3v2_unsynchsafe(p_id3v2 + 6);
}

/** @This gets the footer size of the id3 tag.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the footer size in bytes
 */
static inline uint32_t id3v2_footer_get_size(const uint8_t *p_id3v2)
{
    return id3v2_check_footer(p_id3v2) ?  ID3V2_FOOTER_SIZE : 0;
}

/** @This gets the id3 tag extended header size in bytes.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the extended header size in bytes
 */
static inline uint32_t id3v2_get_extented_header_size(const uint8_t *p_id3v2)
{
    if (!id3v2_check_extented_header(p_id3v2))
        return 0;
    return id3v2_unsynchsafe(p_id3v2 + ID3V2_HEADER_SIZE);
}

/** @This gets the total id3 header (header + extended header) size in bytes.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the header size in bytes
 */
static inline uint32_t id3v2_get_header_size(const uint8_t *p_id3v2)
{
    return ID3V2_HEADER_SIZE + id3v2_get_extented_header_size(p_id3v2);
}

/** @This gets the total id3 tag size in bytes.
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @return the size in bytes
 */
static inline uint32_t id3v2_get_total_size(const uint8_t *p_id3v2)
{
    return id3v2_get_header_size(p_id3v2) +
           id3v2_get_size(p_id3v2) +
           id3v2_footer_get_size(p_id3v2);
}

/** @This represents an id3 frame. */
struct id3v2_frame {
    /** id of the frame */
    char id[4];
    /** size in bytes */
    uint32_t size;
    /** flags */
    uint8_t flags[2];
    /** pointer to the frame data */
    const uint8_t *data;
};

/** @This gets the next frame in the id3 tag.
 *
 * @code C
 * struct id3v2_frame frame;
 * memset(&frame, 0, sizeof (frame));
 * while (id3v2_get_frame(p_id3v2, &frame))
 *      /@* do something with frame *@/;
 * @end code
 *
 * @param p_id3v2 pointer to the id3v2 tag
 * @param frame pointer filled with the frame, @strong must be @ref memset
 * before the first call.
 * @return true if a frame was found, false otherwise
 */
static inline bool id3v2_get_frame(const uint8_t *p_id3v2,
                                   struct id3v2_frame *frame)
{
    const uint8_t *body = p_id3v2 + id3v2_get_header_size(p_id3v2);
    const uint8_t *footer = body + id3v2_get_size(p_id3v2);
    const uint8_t *p = frame->data ? frame->data + frame->size : body;

    if (p >= footer || p[0] == 0)
        return false;

    for (uint8_t i = 0; i < 4; i++)
        frame->id[i] = p[i];
    frame->size = id3v2_unsynchsafe(p + 4);
    for (uint8_t i = 0; i < 2; i++)
        frame->flags[i] = p[8 + i];
    frame->data = p + ID3V2_FRAME_HEADER_SIZE;
    return true;
}

/** @This checks equality for the frame id.
 *
 * @param frame pointer to a frame
 * @param id id to compare to
 * @return true if id is equal, false otherwise
 */
static inline bool id3v2_frame_check_id(const struct id3v2_frame *frame,
                                        const char *id)
{
    for (uint8_t i = 0; i < 4; i++)
        if (frame->id[i] != id[i])
            return false;
    return true;
}

/** @This represents an id3 private frame (frame id "PRIV"). */
struct id3v2_frame_priv {
    /** owner identifier */
    const char *owner;
    /** size of the data */
    uint32_t size;
    /** pointer to the data */
    const uint8_t *data;
};

/** @This gets a private id3 frame.
 *
 * @param frame pointer to the private frame
 * @param priv pointer filled with the private information
 * @return true if frame is a valid private frame, false otherwise
 */
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

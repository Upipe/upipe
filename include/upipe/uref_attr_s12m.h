/*
 * Copyright (C) 2024 Open Broadcast Systems Ltd
 *
 * Authors: James Darnley
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
 * @short Upipe uref s12m attributes handling
 *
 * Helper functions to handle the s12m timecode attributes.  See the SMPTE
 * standards 12M-1 and 314M for more details.
 */

#ifndef _UPIPE_UREF_ATTR_S12M_H_
/** @hidden */
#define _UPIPE_UREF_ATTR_S12M_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_pic.h"

#include <stdbool.h>
#include <stdint.h>

/** @this returns a uint32_t read from possibly unaligned data
 *
 * Timecode packs and the count are native endian values.
 */
static inline uint32_t uref_attr_s12m_read(const uint8_t *data)
{
    /* TODO: big endian */
    return data[0] | data[1] << 8 | data[2] << 16 | (uint32_t)data[3] << 24;
}

/** @This returns true if the data and size given repesents a valid s12m
 * attribute.
 */
static inline bool uref_attr_s12m_check(const uint8_t *data, const size_t size)
{
    /* Size must be at least 4 for a single uint32_t. */
    if (size < 4)
        return false;
    const uint32_t count = uref_attr_s12m_read(data);
    /* The count should be followed by that number of uint32_t. */
    if (size < sizeof(uint32_t) * (count+1))
        return false;
    return true;
}

/** @This returns true if the integer values are permissible in a timecode.
 */
static inline bool uref_attr_s12m_validate_integers(const uint32_t hours,
        const uint32_t minutes, const uint32_t seconds, const uint32_t frames)
{
    if (hours > 23)
        return false;
    if (minutes > 59)
        return false;
    if (seconds > 59)
        return false;
    if (frames > 29)
        return false;
    return true;
}

/** @This returns true if the decimal values are permissible in a timecode.
 */
static inline bool uref_attr_s12m_validate_decimals(const uint32_t hours_10s,
        const uint32_t hours_1s, const uint32_t minutes_10s, const uint32_t minutes_1s,
        const uint32_t seconds_10s, const uint32_t seconds_1s, const uint32_t frames_10s,
        const uint32_t frames_1s)
{
    return uref_attr_s12m_validate_integers(10*hours_10s + hours_1s,
            10*minutes_10s + minutes_1s, 10*seconds_10s + seconds_1s,
            10*frames_10s + frames_1s);
}

/** @This returns a new timecode pack from the integer components and the drop
 * frame flag.  Does not validate values.
 *
 * @param hours 0-23
 * @param minutes 0-59
 * @param seconds 0-59
 * @param frames 0 to framerate
 * @param drop true or false
 * @return timecode pack
 */

static inline uint32_t uref_attr_s12m_from_integers(const uint32_t hours,
        const uint32_t minutes, const uint32_t seconds, const uint32_t frames,
        const uint32_t drop)
{
    return drop << 30
        | hours   % 10
        | hours   / 10 <<  4
        | minutes % 10 <<  8
        | minutes / 10 << 12
        | seconds % 10 << 16
        | seconds / 10 << 20
        | frames  % 10 << 24
        | frames  / 10 << 28;
}

/** @This splits a timecode pack into the integer components and the drop frame
 * flag.  Does not validate.
 * values.
 *
 * @param timecode timecode pack
 * @param hours pointer
 * @param minutes pointer
 * @param seconds pointer
 * @param frames pointer
 * @param drop pointer
 */

static inline void uref_attr_s12m_to_integers(const uint32_t timecode,
        uint8_t *hours, uint8_t *minutes, uint8_t *seconds, uint8_t *frames,
        bool *drop)
{
    *hours =   10 * (timecode >>  4 & 0x3) + (timecode       & 0xf);
    *minutes = 10 * (timecode >> 12 & 0x7) + (timecode >>  8 & 0xf);
    *seconds = 10 * (timecode >> 20 & 0x7) + (timecode >> 16 & 0xf);
    *frames  = 10 * (timecode >> 28 & 0x3) + (timecode >> 24 & 0xf);
    *drop = (timecode & 1<<30) == 1<<30;
}

/** @This returns a new timecode pack from the decimal components and the drop
 * frame flag.  Does not validate values.
 *
 * @param hours_10s 0-2
 * @param hours_1s 0-9
 * @param minutes_10s 0-5
 * @param minutes_1s 0-9
 * @param seconds_10s 0-5
 * @param seconds_1s 0-9
 * @param frames_10s 0 to tens of framerate
 * @param frames_1s 0-9
 * @param drop true or false
 * @return timecode pack
 */

static inline uint32_t uref_attr_s12m_from_decimals(const uint32_t hours_10s,
        const uint32_t hours_1s, const uint32_t minutes_10s, const uint32_t minutes_1s,
        const uint32_t seconds_10s, const uint32_t seconds_1s, const uint32_t frames_10s,
        const uint32_t frames_1s, const uint32_t drop)
{
    return drop << 30
        | hours_1s
        | hours_10s   <<  4
        | minutes_1s  <<  8
        | minutes_10s << 12
        | seconds_1s  << 16
        | seconds_10s << 20
        | frames_1s   << 24
        | frames_10s  << 28;
}

/** @This splits a timecode pack into the decimal components and the drop frame
 * flag.  Does not validate values.
 *
 * @pack timecode timecode pack
 * @param hours_10s pointer
 * @param hours_1s pointer
 * @param minutes_10s pointer
 * @param minutes_1s pointer
 * @param seconds_10s pointer
 * @param seconds_1s pointer
 * @param frames_10s pointer
 * @param frames_1s pointer
 * @param drop pointer
 */

static inline void uref_attr_s12m_to_decimals(const uint32_t timecode,
        uint8_t *hours_10s, uint8_t *hours_1s, uint8_t *minutes_10s, uint8_t *minutes_1s,
        uint8_t *seconds_10s, uint8_t *seconds_1s, uint8_t *frames_10s, uint8_t *frames_1s,
        bool *drop)
{
    *hours_10s   = timecode >>  4 & 0x3;
    *hours_1s    = timecode       & 0xf;
    *minutes_10s = timecode >> 12 & 0x7;
    *minutes_1s  = timecode >>  8 & 0xf;
    *seconds_10s = timecode >> 20 & 0x7;
    *seconds_1s  = timecode >> 16 & 0xf;
    *frames_10s  = timecode >> 28 & 0x3;
    *frames_1s   = timecode >> 24 & 0xf;
    *drop = (timecode & 1 << 30) == 1 << 30;
}

#ifdef __cplusplus
}
#endif
#endif

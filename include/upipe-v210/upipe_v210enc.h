/*
 * V210 encoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 * Copyright (c) 2015 Open Broadcast Systems Ltd
 *
 * This file is based on the implementation in FFmpeg.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** @file
 * @short Upipe v210enc module
 */

#ifndef _UPIPE_MODULES_UPIPE_V210ENC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_V210ENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_V210ENC_SIGNATURE UBASE_FOURCC('v','2','1','e')

/** @This defines an 8-bit packing function. */
typedef void (*upipe_v210enc_pack_line_8)(
        const uint8_t *y, const uint8_t *u, const uint8_t *v,
        uint8_t *dst, ptrdiff_t width);

/** @This defines a 10-bit packing function. */
typedef void (*upipe_v210enc_pack_line_10)(
        const uint16_t *y, const uint16_t *u, const uint16_t *v,
        uint8_t *dst, ptrdiff_t width);

/** @This extends upipe_command with specific commands for v210enc pipes. */
enum upipe_v210enc_command {
    UPIPE_V210ENC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set 8-bit packing function (upipe_v210enc_pack_line_8) */
    UPIPE_V210ENC_SET_PACK_LINE_8,
    /** get 8-bit packing function (upipe_v210enc_pack_line_8 *) */
    UPIPE_V210ENC_GET_PACK_LINE_8,
    /** set 10-bit packing function (upipe_v210enc_pack_line_10) */
    UPIPE_V210ENC_SET_PACK_LINE_10,
    /** get 10-bit packing function (upipe_v210enc_pack_line_10 *) */
    UPIPE_V210ENC_GET_PACK_LINE_10
};

/** @This sets the 8-bit packing function.
 *
 * @param upipe description structure of the pipe
 * @param pack packing function
 * @return an error code
 */
static inline int upipe_v210enc_set_pack_line_8(struct upipe *upipe,
        upipe_v210enc_pack_line_8 pack)
{
    return upipe_control(upipe, UPIPE_V210ENC_SET_PACK_LINE_8,
                         UPIPE_V210ENC_SIGNATURE, pack);
}

/** @This gets the 8-bit packing function.
 *
 * @param upipe description structure of the pipe
 * @param pack_p written with the packing function
 * @return an error code
 */
static inline int upipe_v210enc_get_pack_line_8(struct upipe *upipe,
        upipe_v210enc_pack_line_8 *pack_p)
{
    return upipe_control(upipe, UPIPE_V210ENC_GET_PACK_LINE_8,
                         UPIPE_V210ENC_SIGNATURE, pack_p);
}

/** @This sets the 10-bit packing function.
 *
 * @param upipe description structure of the pipe
 * @param pack packing function
 * @return an error code
 */
static inline int upipe_v210enc_set_pack_line_10(struct upipe *upipe,
        upipe_v210enc_pack_line_10 pack)
{
    return upipe_control(upipe, UPIPE_V210ENC_SET_PACK_LINE_10,
                         UPIPE_V210ENC_SIGNATURE, pack);
}

/** @This gets the 10-bit packing function.
 *
 * @param upipe description structure of the pipe
 * @param pack_p written with the packing function
 * @return an error code
 */
static inline int upipe_v210enc_get_pack_line_10(struct upipe *upipe,
        upipe_v210enc_pack_line_10 *pack_p)
{
    return upipe_control(upipe, UPIPE_V210ENC_GET_PACK_LINE_10,
                         UPIPE_V210ENC_SIGNATURE, pack_p);
}

/** @This returns the management structure for v210 pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_v210enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

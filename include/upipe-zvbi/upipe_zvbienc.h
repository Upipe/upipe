/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe zvbi encoding module
 */

#ifndef _UPIPE_MODULES_UPIPE_ZVBIENC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_ZVBIENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_ZVBIENC_SIGNATURE UBASE_FOURCC('z','v','b','e')

/** @This extends upipe_command with specific commands for avcodec decode. */
enum upipe_zvbienc_command {
    UPIPE_ZVBIENC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set picture format (int) */
    UPIPE_ZVBIENC_SET_PIC_FMT,
    /** get picture format (int *) */
    UPIPE_ZVBIENC_GET_PIC_FMT
};

/** @This gets the zvbienc picture format.
 *
 * @param upipe description structure of the pipe
 * @param flags_p filled in with the zvbienccale flags
 * @return false in case of error
 */
static inline bool upipe_zvbienc_get_flags(struct upipe *upipe, int *pic_fmt)
{
    return upipe_control(upipe, UPIPE_ZVBIENC_GET_PIC_FMT, UPIPE_ZVBIENC_SIGNATURE,
                         pic_fmt);
}

/** @This sets the zvbienc picture format.
 *
 * @param upipe description structure of the pipe
 * @param flags zvbienccale flags
 * @return false in case of error
 */
static inline bool upipe_zvbienc_set_flags(struct upipe *upipe, int pic_fmt)
{
    return upipe_control(upipe, UPIPE_ZVBIENC_SET_PIC_FMT, UPIPE_ZVBIENC_SIGNATURE,
                         pic_fmt);
}

/** @This returns the management structure for zvbi pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_zvbienc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

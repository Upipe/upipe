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

#ifndef _UPIPE_V210_UPIPE_V210ENC_H_
/** @hidden */
#define _UPIPE_V210_UPIPE_V210ENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_V210ENC_SIGNATURE UBASE_FOURCC('v','2','1','e')

/** @This returns the management structure for v210 pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_v210enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * 10 bit packing
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
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
 * @short Upipe pack10bit module
 */

#ifndef _UPIPE_MODULES_UPIPE_PACK10BIT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PACK10BIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_PACK10BIT_SIGNATURE UBASE_FOURCC('p','1','0','b')

#include <upipe/upipe.h>

/** @This returns the management structure for pack10bit pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pack10bit_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

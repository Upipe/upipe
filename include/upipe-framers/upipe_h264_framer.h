/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of an ISO 13818-10-B stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_H264_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_H264_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_H264F_SIGNATURE UBASE_FOURCC('2','6','4','f')
/** We only accept the ISO 14496-10 annex B elementary stream. */
#define UPIPE_H264F_EXPECTED_FLOW_DEF "block.h264."

/** @This returns the management structure for all h264f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_h264f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

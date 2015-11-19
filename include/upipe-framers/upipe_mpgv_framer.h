/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module building frames from chunks of an ISO 13818-2 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_MPGV_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_MPGV_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_MPGVF_SIGNATURE UBASE_FOURCC('m','p','v','f')
/** We only accept the ISO 13818-2 elementary stream. */
#define UPIPE_MPGVF_EXPECTED_FLOW_DEF "block.mpeg2video."

/** @This returns the management structure for all mpgvf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mpgvf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

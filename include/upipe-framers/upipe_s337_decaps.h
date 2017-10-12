/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of a SMPTE 337 stream
 * This pipe only supports the 16-bit mode.
 *
 * Normative references:
 *  - SMPTE 337-2008 (non-PCM in AES3)
 *  - SMPTE 338-2008 (non-PCM in AES3 - data types)
 *  - SMPTE 340-2008 (non-PCM in AES3 - ATSC A/52B)
 */

#ifndef _UPIPE_FRAMERS_UPIPE_S337_DECAPS_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_S337_DECAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_S337D_SIGNATURE UBASE_FOURCC('3','3','7','d')

/** @This returns the management structure for all s337d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s337d_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

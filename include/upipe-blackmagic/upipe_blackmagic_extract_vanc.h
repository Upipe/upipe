/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
*/

/** @file
 * @short Upipe module to extract Blackmagic vertical ancillary data
 */

#ifndef _UPIPE_BLACKMAGIC_UPIPE_BLACKMAGIC_EXTRACT_VANC_H_
/** @hidden */
#define _UPIPE_BLACKMAGIC_UPIPE_BLACKMAGIC_EXTRACT_VANC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_BMD_VANC_SIGNATURE UBASE_FOURCC('b', 'm', 'd', 'v')

/** @This returns the management structure for all bmd_vanc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_vanc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

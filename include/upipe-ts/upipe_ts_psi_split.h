/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module splitting tables of the PSI of a transport stream
 *
 * Please note the special behavior of @ref upipe_split_set_flow_def. If the
 * flow suffix doesn't exist, it creates it. If flow_def is NULL, it deletes
 * it. This function must be called before @ref upipe_split_set_output.
 */

#ifndef _UPIPE_TS_UPIPE_TS_PSI_SPLIT_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PSI_SPLIT_H_

#include <upipe/upipe.h>

#define UPIPE_TS_PSI_SPLIT_SIGNATURE 0x0F100100U

/** @This returns the management structure for all ts_psi_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psi_split_mgr_alloc(void);

#endif

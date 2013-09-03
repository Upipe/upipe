/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe module to aggregate complete TS packets up to specified MTU
 */

#ifndef _UPIPE_TS_UPIPE_TS_AGGREGATE_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_AGGREGATE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TS_AGG_SIGNATURE UBASE_FOURCC('t','s','a','g')

/** @This returns the management structure for all ts_agg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_agg_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

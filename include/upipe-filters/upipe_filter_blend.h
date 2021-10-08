/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe blend deinterlace filter
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_BLEND_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_BLEND_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_FILTER_BLEND_SIGNATURE UBASE_FOURCC('b', 'l', 'e', 'n')

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_blend_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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

#ifndef _UPIPE_FILTERS_UPIPE_ZONEPLATE_SOURCE_H_
#define _UPIPE_FILTERS_UPIPE_ZONEPLATE_SOURCE_H_

#include <upipe/ubase.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @This is the signature of a zoneplate source pipe. */
#define UPIPE_ZONEPLATE_SIGNATURE    UBASE_FOURCC('z','o','n','e')

/** @This returns the zoneplate source pipe manager.
 *
 * @return a pointer to the zoneplate source pipe manager
 */
struct upipe_mgr *upipe_zoneplate_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_FILTERS_UPIPE_ZONEPLATE_SOURCE_H_ */

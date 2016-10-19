/*
 * Copyright (C) 2016 Open Broadcast System Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
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
 * @short Upipe filter computing the maximum amplitude per uref
 */

#ifndef _UPIPE_FILTERS_UPIPE_AUDIO_MAX_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_AUDIO_MAX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe/uref_attr.h>
#include <stdint.h>

UREF_ATTR_FLOAT_VA(amax, amplitude, "amax.amp[%" PRIu8"]", max amplitude,
        uint8_t plane, plane)

#define UPIPE_AUDIO_MAX_SIGNATURE UBASE_FOURCC('a', 'm', 'a', 'x')

/** @This returns the management structure for all amax sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_amax_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module building frames from chunks of a DVB subtitles stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_DVBSUB_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_DVBSUB_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_DVBSUBF_SIGNATURE UBASE_FOURCC('d','s','b','f')
/** We only accept the EN 300 743 elementary stream. */
#define UPIPE_DVBSUBF_EXPECTED_FLOW_DEF "block.dvb_subtitle."

/** @This returns the management structure for all dvbsubf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dvbsubf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

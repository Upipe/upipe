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
 * @short Upipe module building frames from an ISO 13818-3 or 7 stream
 * This framer supports levels 1, 2, 3 of ISO/IEC 11179-3 and ISO/IEC 13818-3,
 * and ISO/IEC 13818-7 (ADTS AAC) streams
 */

#ifndef _UPIPE_FRAMERS_UPIPE_MPGA_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_MPGA_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_MPGAF_SIGNATURE UBASE_FOURCC('m','p','a','f')

/** @This returns the management structure for all mpgaf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mpgaf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

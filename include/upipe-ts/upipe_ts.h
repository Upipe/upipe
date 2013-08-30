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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe declarations common to TS demux and mux
 */

#ifndef _UPIPE_TS_UPIPE_TS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This is the conformance mode of a transport stream. */
enum upipe_ts_conformance {
    /** automatic conformance */
    UPIPE_TS_CONFORMANCE_AUTO,
    /** no conformance, just ISO 13818-1 */
    UPIPE_TS_CONFORMANCE_ISO,
    /** DVB conformance (ETSI EN 300 468) */
    UPIPE_TS_CONFORMANCE_DVB,
    /** ATSC conformance */
    UPIPE_TS_CONFORMANCE_ATSC,
    /** ISDB conformance */
    UPIPE_TS_CONFORMANCE_ISDB
};

#ifdef __cplusplus
}
#endif
#endif

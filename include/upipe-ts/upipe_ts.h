/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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

/** @This returns a string describing the conformance.
 *
 * @param conformance coded conformance
 * @return a constant string describing the conformance
 */
static inline const char *
    upipe_ts_conformance_print(enum upipe_ts_conformance conformance)
{
    switch (conformance) {
        case UPIPE_TS_CONFORMANCE_AUTO: return "auto";
        case UPIPE_TS_CONFORMANCE_ISO: return "ISO";
        case UPIPE_TS_CONFORMANCE_DVB: return "DVB";
        case UPIPE_TS_CONFORMANCE_ATSC: return "ATSC";
        case UPIPE_TS_CONFORMANCE_ISDB: return "ISDB";
        default: return "unknown";
    }
}

#ifdef __cplusplus
}
#endif
#endif

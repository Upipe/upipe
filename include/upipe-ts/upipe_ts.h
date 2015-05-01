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

#include <upipe-ts/uref_ts_flow.h>

/** @This is the conformance mode of a transport stream. */
enum upipe_ts_conformance {
    /** automatic conformance */
    UPIPE_TS_CONFORMANCE_AUTO,
    /** no conformance, just ISO 13818-1 */
    UPIPE_TS_CONFORMANCE_ISO,
    /** DVB conformance without SI tables (ETSI EN 300 468) */
    UPIPE_TS_CONFORMANCE_DVB_NO_TABLES,
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
        case UPIPE_TS_CONFORMANCE_DVB_NO_TABLES: return "DVB (no tables)";
        case UPIPE_TS_CONFORMANCE_DVB: return "DVB";
        case UPIPE_TS_CONFORMANCE_ATSC: return "ATSC";
        case UPIPE_TS_CONFORMANCE_ISDB: return "ISDB";
        default: return "unknown";
    }
}

/** @This encodes a conformance into a flow definition packet.
 *
 * @param flow_def flow definition packet
 * @param conformance coded conformance
 * @return an error code
 */
static inline int upipe_ts_conformance_to_flow_def(struct uref *flow_def,
        enum upipe_ts_conformance conformance)
{
    switch (conformance) {
        default:
            uref_ts_flow_delete_conformance(flow_def);
            return UBASE_ERR_NONE;
        case UPIPE_TS_CONFORMANCE_DVB_NO_TABLES:
            return uref_ts_flow_set_conformance(flow_def, "dvb_no_tables");
        case UPIPE_TS_CONFORMANCE_DVB:
            return uref_ts_flow_set_conformance(flow_def, "dvb");
        case UPIPE_TS_CONFORMANCE_ATSC:
            return uref_ts_flow_set_conformance(flow_def, "atsc");
        case UPIPE_TS_CONFORMANCE_ISDB:
            return uref_ts_flow_set_conformance(flow_def, "isdb");
    }
}

/** @This encodes a conformance from a string.
 *
 * @param conformance string describing the conformance
 * @return conformance coded conformance
 */
static inline enum upipe_ts_conformance
    upipe_ts_conformance_from_string(const char *conformance)
{
    if (conformance == NULL)
        return UPIPE_TS_CONFORMANCE_AUTO;
    if (!strcmp(conformance, "dvb_no_tables"))
        return UPIPE_TS_CONFORMANCE_DVB_NO_TABLES;
    if (!strcmp(conformance, "dvb"))
        return UPIPE_TS_CONFORMANCE_DVB;
    if (!strcmp(conformance, "atsc"))
        return UPIPE_TS_CONFORMANCE_ATSC;
    if (!strcmp(conformance, "isdb"))
        return UPIPE_TS_CONFORMANCE_ISDB;
    return UPIPE_TS_CONFORMANCE_AUTO;
}

/** @This encodes a conformance from a flow definition packet.
 *
 * @param flow_def flow definition packet
 * @return coded conformance
 */
static inline enum upipe_ts_conformance
    upipe_ts_conformance_from_flow_def(struct uref *flow_def)
{
    const char *conformance = NULL;
    uref_ts_flow_get_conformance(flow_def, &conformance);
    return upipe_ts_conformance_from_string(conformance);
}

#ifdef __cplusplus
}
#endif
#endif

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
 * @short Upipe module encapsulating (adding TS header) PES and PSI access units
 */

#ifndef _UPIPE_TS_UPIPE_TS_ENCAPS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_ENCAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_mux.h>

#define UPIPE_TS_ENCAPS_SIGNATURE UBASE_FOURCC('t','s','e','c')

/** @This extends upipe_command with specific commands for ts encaps. */
enum upipe_ts_encaps_command {
    UPIPE_TS_ENCAPS_SENTINEL = UPIPE_TS_MUX_ENCAPS,

    /** returns the date of the next access unit (uint64_t *) */
    UPIPE_TS_ENCAPS_PEEK,
    /** returns the date of the next TS packet (uint64_t, uint64_t *,
     * uint64_t *) */
    UPIPE_TS_ENCAPS_PREPARE,
    /** returns a ubuf containing a TS packet (struct ubuf **, uint64_t *) */
    UPIPE_TS_ENCAPS_SPLICE
};

/** @This returns the date of the next access unit.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys_p filled in with the date of the next access unit
 * @return an error code
 */
static inline int upipe_ts_encaps_peek(struct upipe *upipe, uint64_t *cr_sys_p)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_ENCAPS_PEEK,
                               UPIPE_TS_ENCAPS_SIGNATURE, cr_sys_p);
}

/** @This returns the date of the next TS packet, and deletes all data
 * prior the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys data before cr_sys will be deleted
 * @param cr_sys_p filled in with the date of the next TS packet
 * @param dts_sys_p filled in with the DTS of the next TS packet
 * @return an error code
 */
static inline int upipe_ts_encaps_prepare(struct upipe *upipe, uint64_t cr_sys,
                                          uint64_t *cr_sys_p,
                                          uint64_t *dts_sys_p)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_ENCAPS_PREPARE,
                               UPIPE_TS_ENCAPS_SIGNATURE, cr_sys, cr_sys_p,
                               dts_sys_p);
}

/** @This returns a ubuf containing a TS packet, and the DTS of the packet.
 *
 * @param upipe description structure of the pipe
 * @param ubuf_p filled in with a pointer to the ubuf
 * @param dts_sys_p filled in with the DTS, or UINT64_MAX
 * @return an error code
 */
static inline int upipe_ts_encaps_splice(struct upipe *upipe,
                                         struct ubuf **ubuf_p,
                                         uint64_t *dts_sys_p)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_ENCAPS_SPLICE,
                               UPIPE_TS_ENCAPS_SIGNATURE, ubuf_p, dts_sys_p);
}

/** @This returns the management structure for all ts_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_encaps_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

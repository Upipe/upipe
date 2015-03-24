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

/** @This extends uprobe_event with specific events for ts split. */
enum uprobe_ts_encaps_event {
    UPROBE_TS_ENCAPS_SENTINEL = UPROBE_TS_MUX_ENCAPS,

    /** update status of the encaps pipe (uint64_t, uint64_t, uint64_t, int) */
    UPROBE_TS_ENCAPS_STATUS
};

/** @This extends upipe_command with specific commands for ts encaps. */
enum upipe_ts_encaps_command {
    UPIPE_TS_ENCAPS_SENTINEL = UPIPE_TS_MUX_ENCAPS,

    /** sets the size of the TB buffer (unsigned int) */
    UPIPE_TS_ENCAPS_SET_TB_SIZE,
    /** returns a ubuf containing a TS packet and its dts_sys (uint64_t,
     * struct ubuf **, uint64_t *) */
    UPIPE_TS_ENCAPS_SPLICE,
    /** signals an end of stream (void) */
    UPIPE_TS_ENCAPS_EOS
};

/** @This sets the size of the TB buffer.
 *
 * @param upipe description structure of the pipe
 * @param tb_size size of the TB buffer
 * @return an error code
 */
static inline int upipe_ts_encaps_set_tb_size(struct upipe *upipe,
                                              unsigned int tb_size)
{
    return upipe_control(upipe, UPIPE_TS_ENCAPS_SET_TB_SIZE,
                         UPIPE_TS_ENCAPS_SIGNATURE, tb_size);
}

/** @This returns a ubuf containing a TS packet, and the dts_sys of the packet.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys date at which the packet will be muxed
 * @param ubuf_p filled in with a pointer to the ubuf (may be NULL)
 * @param dts_sys_p filled in with the dts_sys, or UINT64_MAX
 * @return an error code
 */
static inline int upipe_ts_encaps_splice(struct upipe *upipe, uint64_t cr_sys,
                                         struct ubuf **ubuf_p,
                                         uint64_t *dts_sys_p)
{
    return upipe_control_nodbg(upipe, UPIPE_TS_ENCAPS_SPLICE,
                               UPIPE_TS_ENCAPS_SIGNATURE, cr_sys, ubuf_p,
                               dts_sys_p);
}

/** @This signals an end of stream, so that buffered packets can be released.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static inline int upipe_ts_encaps_eos(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_TS_ENCAPS_EOS, UPIPE_TS_ENCAPS_SIGNATURE);
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

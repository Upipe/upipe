/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This service is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This service is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this service; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the time and date table of DVB streams
 * Normative references:
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-ts/upipe_ts_tdt_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <iconv.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtstdt."

/** @internal @This is the private context of a ts_tdtd pipe. */
struct upipe_ts_tdtd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_tdtd, upipe, UPIPE_TS_TDTD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_tdtd, urefcount, upipe_ts_tdtd_free)
UPIPE_HELPER_VOID(upipe_ts_tdtd)

/** @internal @This allocates a ts_tdtd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_tdtd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_tdtd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_tdtd_init_urefcount(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_tdtd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    uint8_t buffer[TDT_HEADER_SIZE];
    const uint8_t *tdt = uref_block_peek(uref, 0, TDT_HEADER_SIZE, buffer);
    assert(tdt != NULL);

    if (unlikely(!tdt_validate(tdt))) {
        upipe_warn(upipe, "invalid TDT section received");
        uref_block_peek_unmap(uref, 0, buffer, tdt);
        uref_free(uref);
        return;
    }

    uint64_t utc = tdt_get_utc(tdt);
    uref_block_peek_unmap(uref, 0, buffer, tdt);

    char date[24];
    time_t time = dvb_time_format_UTC(utc, NULL, date);
    upipe_dbg_va(upipe, "throw UTC clock (%s)", date);
    upipe_throw_clock_utc(upipe, uref, UCLOCK_FREQ * time);
    uref_free(uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_tdtd_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_tdtd_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_tdtd_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_tdtd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_tdtd_clean_urefcount(upipe);
    upipe_ts_tdtd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_tdtd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_TDTD_SIGNATURE,

    .upipe_alloc = upipe_ts_tdtd_alloc,
    .upipe_input = upipe_ts_tdtd_input,
    .upipe_control = upipe_ts_tdtd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_tdtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_tdtd_mgr_alloc(void)
{
    return &upipe_ts_tdtd_mgr;
}

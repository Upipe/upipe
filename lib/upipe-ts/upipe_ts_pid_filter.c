/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module filtering on PIDs of a transport stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_pid_filter.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks containing exactly one TS packet */
#define EXPECTED_FLOW_DEF "block.mpegts."
/** maximum number of PIDs */
#define MAX_PIDS 8192

/** @internal @This is the private context of a ts split pipe. */
struct upipe_ts_pidf {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet on this output */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** enabled PIDs array */
    uint8_t enabled_pids[MAX_PIDS / 8];

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pidf, upipe, UPIPE_TS_PIDF_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_pidf, urefcount, upipe_ts_pidf_free)
UPIPE_HELPER_VOID(upipe_ts_pidf)
UPIPE_HELPER_OUTPUT(upipe_ts_pidf, output, flow_def, output_state, request_list)

/** @internal @This allocates a ts_pidf pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pidf_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_pidf_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_pidf *upipe_ts_pidf = upipe_ts_pidf_from_upipe(upipe);
    upipe_ts_pidf_init_urefcount(upipe);
    upipe_ts_pidf_init_output(upipe);

    int i;
    for (i = 0; i < MAX_PIDS / 8; i++)
        upipe_ts_pidf->enabled_pids[i] = 0;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This demuxes a TS packet to the appropriate output(s).
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_pidf_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_ts_pidf *upipe_ts_pidf = upipe_ts_pidf_from_upipe(upipe);
    uint8_t buffer[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, TS_HEADER_SIZE,
                                               buffer);
    if (unlikely(ts_header == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uint16_t pid = ts_get_pid(ts_header);
    UBASE_FATAL(upipe, uref_block_peek_unmap(uref, 0, buffer, ts_header))

    if (upipe_ts_pidf->enabled_pids[pid / 8] & (1 << (pid & 0x7)))
        upipe_ts_pidf_output(upipe, uref, upump_p);
    else
        uref_free(uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_pidf_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def);
    upipe_ts_pidf_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @This adds the given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid pid to add
 * @return an error code
 */
static int _upipe_ts_pidf_add_pid(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_pidf *upipe_ts_pidf = upipe_ts_pidf_from_upipe(upipe);
    upipe_ts_pidf->enabled_pids[pid / 8] |= 1 << (pid & 0x7);
    return UBASE_ERR_NONE;
}

/** @This deletes the given PID.
 *
 * @param upipe description structure of the pipe
 * @param pid pid to delete
 * @return an error code
 */
static int _upipe_ts_pidf_del_pid(struct upipe *upipe, uint16_t pid)
{
    struct upipe_ts_pidf *upipe_ts_pidf = upipe_ts_pidf_from_upipe(upipe);
    upipe_ts_pidf->enabled_pids[pid / 8] &= ~((uint8_t)1 << (pid & 0x7));
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_pidf_control(struct upipe *upipe,
                                 int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_pidf_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_pidf_set_flow_def(upipe, flow_def);
        }

        case UPIPE_TS_PIDF_ADD_PID:
        case UPIPE_TS_PIDF_DEL_PID: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PIDF_SIGNATURE)
            unsigned int pid = va_arg(args, unsigned int);
            if (command == UPIPE_TS_PIDF_ADD_PID)
                return _upipe_ts_pidf_add_pid(upipe, pid);
            else
                return _upipe_ts_pidf_del_pid(upipe, pid);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pidf_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_pidf_clean_output(upipe);
    upipe_ts_pidf_clean_urefcount(upipe);
    upipe_ts_pidf_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pidf_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PIDF_SIGNATURE,

    .upipe_alloc = upipe_ts_pidf_alloc,
    .upipe_input = upipe_ts_pidf_input,
    .upipe_control = upipe_ts_pidf_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_pidf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pidf_mgr_alloc(void)
{
    return &upipe_ts_pidf_mgr;
}

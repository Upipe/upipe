/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Christophe Massiot
 *
 * This event is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This event is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this event; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module handling the splice information table of SCTE streams
 * Normative references:
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#include "upipe/ubase.h"
#include "upipe/ulist.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_flow.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_uprobe.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_bin_input.h"
#include "upipe-ts/upipe_ts_scte35_merge.h"
#include "upipe-ts/upipe_ts_scte35_probe.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe-ts/uref_ts_scte35.h"
#include "upipe-ts/uref_ts_scte35_desc.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include <bitstream/scte/35.h>

/** we only accept SCTE 35 metadata */
#define EXPECTED_FLOW_DEF "void.scte35."

/** @internal @This is the private context of a ts_scte35p pipe. */
struct upipe_ts_scte35p {
    /** external refcount management structure */
    struct urefcount urefcount;
    /** internal refcount management structure */
    struct urefcount urefcount_real;

    /** scte35 merge bin pipe */
    struct upipe *scte35m;
    /** input request list */
    struct uchain request_list;

    /** scte35 merge probe */
    struct uprobe proxy_probe;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_ts_scte35p_catch_scte35m(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_ts_scte35p, upipe, UPIPE_TS_SCTE35P_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte35p, urefcount, upipe_ts_scte35p_no_ref)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_ts_scte35p, urefcount_real,
                            upipe_ts_scte35p_free)
UPIPE_HELPER_VOID(upipe_ts_scte35p)
UPIPE_HELPER_UPROBE(upipe_ts_scte35p, urefcount_real, proxy_probe,
                    upipe_ts_scte35p_catch_scte35m);
UPIPE_HELPER_INNER(upipe_ts_scte35p, scte35m)
UPIPE_HELPER_BIN_INPUT(upipe_ts_scte35p, scte35m, request_list)

/** @internal @This catches event from the scte35m pipe.
 *
 * @param uprobe structure used to raise events
 * @param upipe description structure of the inner pipe
 * @param event event triggered by the inner pipe
 * @param args optional arguments
 * @return an error code
 */
static int upipe_ts_scte35p_catch_scte35m(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe *upipe = upipe_ts_scte35p_to_upipe(
        upipe_ts_scte35p_from_proxy_probe(uprobe));

    if (ubase_get_signature(args) == UPIPE_TS_SCTE35M_SIGNATURE) {
        if (event == UPROBE_TS_SCTE35M_EXPIRED) {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SCTE35M_SIGNATURE);
            struct uref *uref = va_arg(args, struct uref *);
            uint8_t type;
            enum uprobe_ts_scte35p_event event = UPROBE_TS_SCTE35P_SENTINEL;
            if (ubase_check(uref_ts_scte35_get_command_type(uref, &type))) {
                switch (type) {
                    case SCTE35_NULL_COMMAND:
                        event = UPROBE_TS_SCTE35P_NULL;
                        break;
                    case SCTE35_INSERT_COMMAND:
                        event = UPROBE_TS_SCTE35P_EVENT;
                        break;
                    case SCTE35_TIME_SIGNAL_COMMAND:
                        event = UPROBE_TS_SCTE35P_SIGNAL;
                        break;
                }
            }
            if (event != UPROBE_TS_SCTE35P_SENTINEL)
                return upipe_throw(upipe, event, UPIPE_TS_SCTE35P_SIGNATURE, uref);
        }
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a ts_scte35p pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte35p_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_scte35p_alloc_void(mgr, uprobe, signature,
                                                      args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);
    upipe_ts_scte35p_init_urefcount(upipe);
    upipe_ts_scte35p_init_urefcount_real(upipe);
    upipe_ts_scte35p_init_proxy_probe(upipe);
    upipe_ts_scte35p_init_bin_input(upipe);

    upipe_throw_ready(upipe);

    struct upipe_mgr *upipe_ts_scte35m_mgr = upipe_ts_scte35m_mgr_alloc();
    struct upipe *scte35m = upipe_void_alloc(
        upipe_ts_scte35m_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_ts_scte35p->proxy_probe),
            UPROBE_LOG_VERBOSE, "scte35m"));
    upipe_mgr_release(upipe_ts_scte35m_mgr);
    upipe_ts_scte35p_store_bin_input(upipe, scte35m);
    upipe_attach_upump_mgr(scte35m);

    return upipe;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35p_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_ts_scte35p_control_bin_input(upipe, command, args));
    switch (command) {
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35p_free(struct upipe *upipe)
{
    upipe_ts_scte35p_clean_proxy_probe(upipe);
    upipe_ts_scte35p_clean_urefcount_real(upipe);
    upipe_ts_scte35p_clean_urefcount(upipe);
    upipe_ts_scte35p_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35p_no_ref(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_scte35p_clean_bin_input(upipe);
    upipe_ts_scte35p_release_urefcount_real(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte35p_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE35P_SIGNATURE,

    .upipe_alloc = upipe_ts_scte35p_alloc,
    .upipe_input = upipe_ts_scte35p_bin_input,
    .upipe_control = upipe_ts_scte35p_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_scte35p pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35p_mgr_alloc(void)
{
    return &upipe_ts_scte35p_mgr;
}

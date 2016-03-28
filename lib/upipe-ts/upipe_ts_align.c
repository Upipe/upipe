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
 * @short Upipe module outputting one aligned TS packet per uref
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe-ts/upipe_ts_align.h>
#include <upipe-ts/upipe_ts_sync.h>
#include <upipe-ts/upipe_ts_check.h>
#include <upipe-modules/upipe_idem.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

/** we only accept all kinds of blocks */
#define EXPECTED_FLOW_DEF "block."
/** but already sync'ed TS packets are better */
#define EXPECTED_FLOW_DEF_SYNC "block.mpegts."
/** or otherwise aligned TS packets to check */
#define EXPECTED_FLOW_DEF_CHECK "block.mpegtsaligned."

/** @internal @This is the private context of a ts_align pipe. */
struct upipe_ts_align {
    /** refcount management structure */
    struct urefcount urefcount;

    /** proxy probe */
    struct uprobe proxy_probe;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** first inner pipe of the bin */
    struct upipe *first_inner;
    /** last inner pipe of the bin */
    struct upipe *last_inner;
    /** output */
    struct upipe *output;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_align, upipe, UPIPE_TS_ALIGN_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_align, urefcount, upipe_ts_align_free)
UPIPE_HELPER_VOID(upipe_ts_align)
UPIPE_HELPER_INNER(upipe_ts_align, first_inner)
UPIPE_HELPER_BIN_INPUT(upipe_ts_align, first_inner, input_request_list)
UPIPE_HELPER_INNER(upipe_ts_align, last_inner)
UPIPE_HELPER_BIN_OUTPUT(upipe_ts_align, last_inner, output, output_request_list)

/** @internal @This catches events coming from an inner pipe, and
 * attaches them to the bin pipe.
 *
 * @param uprobe pointer to the probe in upipe_ts_align_alloc
 * @param inner pointer to the inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_ts_align_proxy_probe(struct uprobe *uprobe,
                                      struct upipe *inner,
                                      int event, va_list args)
{
    struct upipe_ts_align *s = container_of(uprobe, struct upipe_ts_align,
                                            proxy_probe);
    struct upipe *upipe = upipe_ts_align_to_upipe(s);
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates a ts_align pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_align_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_align_alloc_void(mgr, uprobe, signature,
                                                    args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_align *upipe_ts_align = upipe_ts_align_from_upipe(upipe);
    upipe_ts_align_init_urefcount(upipe);
    upipe_ts_align_init_bin_input(upipe);
    upipe_ts_align_init_bin_output(upipe);

    uprobe_init(&upipe_ts_align->proxy_probe, upipe_ts_align_proxy_probe, NULL);
    /* Because there is no buffering inside any of the inner pipes. */
    upipe_ts_align->proxy_probe.refcount = NULL;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_align_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_align *upipe_ts_align = upipe_ts_align_from_upipe(upipe);
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;

    struct upipe_mgr *inner_mgr;
    const char *inner_name;
    if (!ubase_ncmp(def, EXPECTED_FLOW_DEF_SYNC)) {
        inner_mgr = upipe_idem_mgr_alloc();
        inner_name = "idem";
    } else if (!ubase_ncmp(def, EXPECTED_FLOW_DEF_CHECK)) {
        inner_mgr = upipe_ts_check_mgr_alloc();
        inner_name = "check";
    } else {
        inner_mgr = upipe_ts_sync_mgr_alloc();
        inner_name = "sync";
    }

    if (unlikely(inner_mgr == NULL))
        return UBASE_ERR_ALLOC;

            /* allocate ts_check inner pipe */
    struct upipe *inner = upipe_void_alloc(inner_mgr,
                     uprobe_pfx_alloc(
                         uprobe_use(&upipe_ts_align->proxy_probe),
                         UPROBE_LOG_VERBOSE, inner_name));
    upipe_mgr_release(inner_mgr);
    upipe_ts_align_store_bin_input(upipe, upipe_use(inner));
    upipe_ts_align_store_bin_output(upipe, inner);
    return upipe_set_flow_def(inner, flow_def);
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_align_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_align_set_flow_def(upipe, flow_def);
        }
    }

    int err = upipe_ts_align_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_ts_align_control_bin_output(upipe, command, args);
    return err;
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_align_free(struct upipe *upipe)
{
    struct upipe_ts_align *upipe_ts_align = upipe_ts_align_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_ts_align_clean_bin_input(upipe);
    upipe_ts_align_clean_bin_output(upipe);
    uprobe_clean(&upipe_ts_align->proxy_probe);
    upipe_ts_align_clean_urefcount(upipe);
    upipe_ts_align_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_align_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_ALIGN_SIGNATURE,

    .upipe_alloc = upipe_ts_align_alloc,
    .upipe_input = upipe_ts_align_bin_input,
    .upipe_control = upipe_ts_align_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_align pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_align_mgr_alloc(void)
{
    return &upipe_ts_align_mgr;
}

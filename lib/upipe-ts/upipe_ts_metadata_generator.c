/*
 * Copyright (C) 2023 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module generating metadata
 */

#include "upipe/ubase.h"
#include "upipe/ulist.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe-ts/upipe_ts_metadata_generator.h"
#include "upipe-ts/upipe_ts_mux.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>

/** @internal @This is the private context of a TS metadata generator pipe. */
struct upipe_ts_mdg {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;

    /** current metadata */
    struct uref *metadata;

    /** metadata interval */
    uint64_t interval;

    /** last window */
    uint64_t last;
    /** current size in the window */
    uint64_t size;
    /** max octetrate */
    uint64_t max_octetrate;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_ts_mdg_check(struct upipe *upipe, struct uref *flow_def);
/** @hidden */
static void upipe_ts_mdg_upump_cb(struct upump *upump);

UPIPE_HELPER_UPIPE(upipe_ts_mdg, upipe, UPIPE_TS_MDG_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_ts_mdg, urefcount, upipe_ts_mdg_free);
UPIPE_HELPER_VOID(upipe_ts_mdg);
UPIPE_HELPER_OUTPUT(upipe_ts_mdg, output, flow_def, output_state,
                    request_list);
UPIPE_HELPER_UCLOCK(upipe_ts_mdg, uclock, uclock_request,
                    upipe_ts_mdg_check,
                    upipe_ts_mdg_register_output_request,
                    upipe_ts_mdg_unregister_output_request);
UPIPE_HELPER_UPUMP_MGR(upipe_ts_mdg, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_ts_mdg, upump, upump_mgr);

/** @internal @This allocates a ts_mdg pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_mdg_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_ts_mdg_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);
    upipe_ts_mdg_init_urefcount(upipe);
    upipe_ts_mdg_init_output(upipe);
    upipe_ts_mdg_init_uclock(upipe);
    upipe_ts_mdg_init_upump_mgr(upipe);
    upipe_ts_mdg_init_upump(upipe);

    upipe_ts_mdg->metadata = NULL;
    upipe_ts_mdg->interval = 0;
    upipe_ts_mdg->last = UINT64_MAX;
    upipe_ts_mdg->size = 0;
    upipe_ts_mdg->max_octetrate = 0;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mdg_free(struct upipe *upipe)
{
    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_ts_mdg->metadata);
    upipe_ts_mdg_clean_upump(upipe);
    upipe_ts_mdg_clean_upump_mgr(upipe);
    upipe_ts_mdg_clean_uclock(upipe);
    upipe_ts_mdg_clean_output(upipe);
    upipe_ts_mdg_clean_urefcount(upipe);
    upipe_ts_mdg_free_void(upipe);
}

/** @internal @This sends a metadata or wait for the next run.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_mdg_send(struct upipe *upipe)
{
    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);
    uint64_t now = upipe_ts_mdg_now(upipe);
    struct uref *uref = upipe_ts_mdg->metadata;
    uint64_t cr_sys = 0;
    uint64_t pts_prog = 0;

    if (!uref)
        return;
    if (now == UINT64_MAX)
        return;

    uref_clock_get_cr_sys(uref, &cr_sys);
    uref_clock_get_pts_prog(uref, &pts_prog);

    if (cr_sys > now) {
        upipe_ts_mdg_wait_upump(upipe, cr_sys - now, upipe_ts_mdg_upump_cb);
        return;
    }

    /* compute max octetrate */
    size_t size = 0;
    uref_block_size(uref, &size);
    if (upipe_ts_mdg->last == UINT64_MAX ||
        upipe_ts_mdg->last + UCLOCK_FREQ < now) {
        upipe_ts_mdg->last = now;
        upipe_ts_mdg->size = 0;
    }
    upipe_ts_mdg->size += size;
    if (upipe_ts_mdg->size > upipe_ts_mdg->max_octetrate) {
        struct uref *flow_def = uref_dup(upipe_ts_mdg->flow_def);
        uref_block_flow_set_max_octetrate(flow_def, upipe_ts_mdg->size);
        uref_block_flow_set_max_buffer_size(flow_def, upipe_ts_mdg->size);
        upipe_ts_mdg_store_flow_def(upipe, flow_def);
        upipe_ts_mdg->max_octetrate = upipe_ts_mdg->size;
    }

    unsigned intervals = (now - cr_sys) / upipe_ts_mdg->interval;
    cr_sys += intervals * upipe_ts_mdg->interval;
    pts_prog += intervals * upipe_ts_mdg->interval;
    uref_clock_set_cr_sys(uref, cr_sys);
    uref_clock_set_pts_prog(uref, pts_prog);

    upipe_ts_mdg_output(upipe, uref_dup(uref), &upipe_ts_mdg->upump);

    if (!upipe_ts_mdg->interval)
        return;

    cr_sys += upipe_ts_mdg->interval;
    pts_prog += upipe_ts_mdg->interval;
    uref_clock_set_cr_sys(uref, cr_sys);
    uref_clock_set_pts_prog(uref, pts_prog);

    uint64_t wait = cr_sys > now ? cr_sys - now : 0;
    upipe_ts_mdg_wait_upump(upipe, wait, upipe_ts_mdg_upump_cb);
}

/** @internal @This is called by the pump.
 *
 * @param upump timer structure
 */
static void upipe_ts_mdg_upump_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_ts_mdg_send(upipe);
}

/** @internal @This handles input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure describing the metadata buffer
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_mdg_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);

    uref_free(upipe_ts_mdg->metadata);
    upipe_ts_mdg->metadata = uref;
    upipe_ts_mdg_send(upipe);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_mdg_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "block.id3."))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    int ret = uref_flow_set_def(flow_def_dup, "block.id3.metadata.");
    if (unlikely(!ubase_check(ret))) {
        uref_free(flow_def_dup);
        upipe_throw_fatal(upipe, ret);
        return ret;
    }

    upipe_ts_mdg_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current metadata interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_mdg_get_interval(struct upipe *upipe, uint64_t *interval_p)
{
    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);
    if (interval_p)
        *interval_p = upipe_ts_mdg->interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the metadata interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_mdg_set_interval(struct upipe *upipe, uint64_t interval)
{
    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);
    if (upipe_ts_mdg->interval != interval) {
        upipe_ts_mdg->interval = interval;
        upipe_ts_mdg_set_upump(upipe, NULL);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mdg_control_real(struct upipe *upipe,
                                     int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_mdg_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_ts_mdg_set_upump(upipe, NULL);
            return upipe_ts_mdg_attach_upump_mgr(upipe);
        }

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_mdg_set_flow_def(upipe, flow_def);
        }

        case UPIPE_TS_MUX_GET_MD_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_mdg_get_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_MD_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_mdg_set_interval(upipe, interval);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This checks the pipe internal state.
 *
 * @param upipe description structure of the pipe
 * @param flow_def optional flow format
 * @return an error code
 */
static int upipe_ts_mdg_check(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_ts_mdg *upipe_ts_mdg = upipe_ts_mdg_from_upipe(upipe);

    if (flow_def)
        uref_free(flow_def);

    if (unlikely(!upipe_ts_mdg->uclock)) {
        upipe_ts_mdg_require_uclock(upipe);
        return UBASE_ERR_NONE;
    }

    UBASE_RETURN(upipe_ts_mdg_check_upump_mgr(upipe));

    if (upipe_ts_mdg->upump_mgr && !upipe_ts_mdg->upump)
        upipe_ts_mdg_send(upipe);

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands and checks the pipe internal
 * state.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_mdg_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(upipe_ts_mdg_control_real(upipe, command, args));
    return upipe_ts_mdg_check(upipe, NULL);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_mdg_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_MDG_SIGNATURE,

    .upipe_alloc = upipe_ts_mdg_alloc,
    .upipe_input = upipe_ts_mdg_input,
    .upipe_control = upipe_ts_mdg_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_mdg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_mdg_mgr_alloc(void)
{
    return &upipe_ts_mdg_mgr;
}

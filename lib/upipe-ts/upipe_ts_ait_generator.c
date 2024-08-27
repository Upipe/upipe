/*
 * Copyright (C) 2024 EasyTools S.A.S.
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module generating the application information table of DVB
 * streams
 * Normative references:
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 *  - ETSI TS 102 809 V1.1.1 (2010-01) (Signalling and carriage of interactive
 *  applications and services)
 */

#include "upipe/ubase.h"
#include "upipe/uclock.h"
#include "upipe/ulist.h"
#include "upipe/uref.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe-ts/upipe_ts_ait_generator.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtsait."
#define OUTPUT_FLOW_DEF "block.mpegtspsi.mpegtsait."

/** @hidden */
static int upipe_ts_aitg_check(struct upipe *upipe, struct uref *flow_format);

/** @hidden */
static void upipe_ts_aitg_schedule(struct upipe *upipe);

/** @internal @This is the private context of a ts_aitg pipe. */
struct upipe_ts_aitg {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** AIT output interval */
    uint64_t interval;
    /** list of urefs composing the current AIT */
    struct uchain ait;
    /** AIT size */
    uint64_t size;

    /** last output date */
    uint64_t last_cr_sys;
    /** compute octetrate */
    uint64_t octetrate;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_aitg, upipe, UPIPE_TS_AITG_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_aitg, urefcount, upipe_ts_aitg_free)
UPIPE_HELPER_VOID(upipe_ts_aitg)
UPIPE_HELPER_UPUMP_MGR(upipe_ts_aitg, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_ts_aitg, upump, upump_mgr)
UPIPE_HELPER_OUTPUT(upipe_ts_aitg, output, flow_def, output_state, request_list)
UPIPE_HELPER_UCLOCK(upipe_ts_aitg, uclock, uclock_request, NULL,
                    upipe_ts_aitg_register_output_request,
                    upipe_ts_aitg_unregister_output_request);

/** @internal @This allocates a ts_aitg pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_aitg_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_aitg_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_aitg *upipe_ts_aitg = upipe_ts_aitg_from_upipe(upipe);
    upipe_ts_aitg_init_urefcount(upipe);
    upipe_ts_aitg_init_uclock(upipe);
    upipe_ts_aitg_init_upump_mgr(upipe);
    upipe_ts_aitg_init_upump(upipe);
    upipe_ts_aitg_init_output(upipe);
    ulist_init(&upipe_ts_aitg->ait);
    upipe_ts_aitg->size = 0;
    upipe_ts_aitg->interval = UCLOCK_FREQ;
    upipe_ts_aitg->last_cr_sys = 0;
    upipe_ts_aitg->octetrate = 1;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This flushes the current AIT if any.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitg_flush(struct upipe *upipe)
{
    struct upipe_ts_aitg *upipe_ts_aitg = upipe_ts_aitg_from_upipe(upipe);
    struct uchain *uchain;

    while ((uchain = ulist_pop(&upipe_ts_aitg->ait)))
        uref_free(uref_from_uchain(uchain));
    upipe_ts_aitg->size = 0;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitg_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_aitg_flush(upipe);
    upipe_ts_aitg_clean_output(upipe);
    upipe_ts_aitg_clean_upump(upipe);
    upipe_ts_aitg_clean_upump_mgr(upipe);
    upipe_ts_aitg_clean_uclock(upipe);
    upipe_ts_aitg_clean_urefcount(upipe);
    upipe_ts_aitg_free_void(upipe);
}

/** @internal @This outputs the AIT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitg_send(struct upipe *upipe)
{
    struct upipe_ts_aitg *upipe_ts_aitg = upipe_ts_aitg_from_upipe(upipe);
    uint64_t now = upipe_ts_aitg_now(upipe);
    struct uchain *uchain;

    /* duplicate AIT */
    struct uchain urefs;
    ulist_init(&urefs);
    ulist_foreach(&upipe_ts_aitg->ait, uchain) {
        struct uref *uref = uref_dup(uref_from_uchain(uchain));
        if (unlikely(!uref)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            while ((uchain = ulist_pop(&urefs)))
                uref_free(uref_from_uchain(uchain));
            return;
        }
        ulist_add(&urefs, uref_to_uchain(uref));
    }

    /* send duplicated AIT */
    upipe_use(upipe);
    upipe_ts_aitg->last_cr_sys = now;
    while ((uchain = ulist_pop(&urefs))) {
        struct uref *uref = uref_from_uchain(uchain);
        uref_clock_set_cr_sys(uref, now);
        upipe_ts_aitg_output(upipe, uref, &upipe_ts_aitg->upump);
    }
    if (!upipe_single(upipe))
        upipe_ts_aitg_schedule(upipe);
    upipe_release(upipe);
}

/** @internal @This is called when the timer timeout.
 *
 * @param upump description structure of the timer
 */
static void upipe_ts_aitg_upump_cb(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_ts_aitg_send(upipe);
}

/** @internal @This schedules the next output of the AIT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitg_schedule(struct upipe *upipe)
{
    struct upipe_ts_aitg *upipe_ts_aitg = upipe_ts_aitg_from_upipe(upipe);
    uint64_t now = upipe_ts_aitg_now(upipe);
    if (now == UINT64_MAX)
        return;

    uint64_t next_cr_sys = upipe_ts_aitg->last_cr_sys + upipe_ts_aitg->interval;
    if (next_cr_sys <= now)
        upipe_ts_aitg_send(upipe);
    else
        upipe_ts_aitg_wait_upump(upipe, next_cr_sys - now,
                                 upipe_ts_aitg_upump_cb);
}

/** @internal @This is called when a new AIT is ready.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitg_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_aitg *upipe_ts_aitg = upipe_ts_aitg_from_upipe(upipe);

    size_t size = 0;
    uref_block_size(uref, &size);
    upipe_ts_aitg->size += size;
    ulist_add(&upipe_ts_aitg->ait, uref_to_uchain(uref));

    if (!ubase_check(uref_block_get_end(uref)))
        return;

    uint64_t octetrate = upipe_ts_aitg->size * UCLOCK_FREQ /
        upipe_ts_aitg->interval;
    if (octetrate > upipe_ts_aitg->octetrate) {
        struct uref *flow_def = uref_dup(upipe_ts_aitg->flow_def);
        uref_block_flow_set_octetrate(flow_def, octetrate);
        upipe_ts_aitg_store_flow_def(upipe, flow_def);
        upipe_ts_aitg->octetrate = octetrate;
    }

    upipe_ts_aitg_schedule(upipe);
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_aitg_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    upipe_ts_aitg_set_upump(upipe, NULL);

    if (ubase_check(uref_block_get_start(uref)))
        upipe_ts_aitg_flush(upipe);

    upipe_ts_aitg_work(upipe, uref);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_aitg_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_aitg_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_aitg_control_real(struct upipe *upipe, int command,
                                      va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_aitg_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_ts_aitg_set_upump(upipe, NULL);
            upipe_ts_aitg_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_ts_aitg_set_upump(upipe, NULL);
            return upipe_ts_aitg_attach_upump_mgr(upipe);

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_aitg_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This checks the internal state of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_aitg_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_ts_aitg *upipe_ts_aitg = upipe_ts_aitg_from_upipe(upipe);

    upipe_ts_aitg_check_upump_mgr(upipe);
    if (unlikely(!upipe_ts_aitg->uclock))
        upipe_ts_aitg_require_uclock(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands and checks the internal state.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_aitg_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(upipe_ts_aitg_control_real(upipe, command, args));
    return upipe_ts_aitg_check(upipe, NULL);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_aitg_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_AITG_SIGNATURE,

    .upipe_alloc = upipe_ts_aitg_alloc,
    .upipe_input = upipe_ts_aitg_input,
    .upipe_control = upipe_ts_aitg_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_aitg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_aitg_mgr_alloc(void)
{
    return &upipe_ts_aitg_mgr;
}

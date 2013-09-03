/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module inserting PSI tables inside a TS stream
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_psi_inserter.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/upipe_ts_mux.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** default interval for PSI tables */
#define DEFAULT_INTERVAL (UCLOCK_FREQ / 10)
/** default delay for PSI tables */
#define DEFAULT_DELAY (UCLOCK_FREQ / 100)

/** @internal @This is the private context of a ts_psii manager. */
struct upipe_ts_psii_mgr {
    /** pointer to ts_encaps manager */
    struct upipe_mgr *ts_encaps_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

/** @internal @This returns the high-level upipe_mgr structure.
 *
 * @param ts_psii_mgr pointer to the upipe_ts_psii_mgr structure
 * @return pointer to the upipe_mgr structure
 */
static inline struct upipe_mgr *
    upipe_ts_psii_mgr_to_upipe_mgr(struct upipe_ts_psii_mgr *ts_psii_mgr)
{
    return &ts_psii_mgr->mgr;
}

/** @internal @This returns the private upipe_ts_psii_mgr structure.
 *
 * @param mgr description structure of the upipe manager
 * @return pointer to the upipe_ts_psii_mgr structure
 */
static inline struct upipe_ts_psii_mgr *
    upipe_ts_psii_mgr_from_upipe_mgr(struct upipe_mgr *mgr)
{
    return container_of(mgr, struct upipe_ts_psii_mgr, mgr);
}

/** @internal @This is the private context of a ts psii pipe. */
struct upipe_ts_psii {
    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** probe for uref_mgr and ubuf_mgr */
    struct uprobe probe;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** list of input subpipes */
    struct ulist subs;

    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psii, upipe, UPIPE_TS_PSII_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_psii, "block.mpegts.")
UPIPE_HELPER_UREF_MGR(upipe_ts_psii, uref_mgr)
UPIPE_HELPER_UBUF_MGR(upipe_ts_psii, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_ts_psii, output, flow_def, flow_def_sent)

/** @internal @This is the private context of a program of a ts_psii pipe. */
struct upipe_ts_psii_sub {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** configured interval */
    uint64_t interval;

    /** latest table */
    struct ulist table;
    /** DTS of the next table occurrence */
    uint64_t next_dts;
    /** DTS (in system time) of the next table occurrence */
    uint64_t next_dts_sys;

    /** pointer to ts_encaps pipe */
    struct upipe *encaps;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psii_sub, upipe, UPIPE_TS_PSII_SUB_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_psii_sub, "block.mpegtspsi.")

UPIPE_HELPER_SUBPIPE(upipe_ts_psii, upipe_ts_psii_sub, sub, sub_mgr,
                     subs, uchain)

/** @internal @This allocates an input subpipe of a ts_psii pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psii_sub_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psii_sub_alloc_flow(mgr, uprobe, signature,
                                                       args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);
    upipe_ts_psii_sub_init_sub(upipe);
    upipe_ts_psii_sub->interval = DEFAULT_INTERVAL;
    ulist_init(&upipe_ts_psii_sub->table);
    upipe_ts_psii_sub->next_dts = upipe_ts_psii_sub->next_dts_sys = UINT64_MAX;
    ulist_init(&upipe_ts_psii_sub->table);

    struct upipe_ts_psii *upipe_ts_psii =
        upipe_ts_psii_from_sub_mgr(upipe->mgr);
    upipe_use(upipe_ts_psii_to_upipe(upipe_ts_psii));

    upipe_throw_ready(upipe);

    struct upipe_ts_psii_mgr *ts_psii_mgr =
        upipe_ts_psii_mgr_from_upipe_mgr(upipe_ts_psii_to_upipe(upipe_ts_psii)->mgr);

    upipe_ts_psii_sub->encaps = upipe_flow_alloc(ts_psii_mgr->ts_encaps_mgr,
                         uprobe_pfx_adhoc_alloc(&upipe_ts_psii->probe,
                                                UPROBE_LOG_DEBUG, "encaps"),
                         flow_def);
    uref_free(flow_def);
    if (unlikely(upipe_ts_psii_sub->encaps == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return upipe;
    }
    if (likely(upipe_ts_psii->output != NULL))
        upipe_set_output(upipe_ts_psii_sub->encaps, upipe_ts_psii->output);

    return upipe;
}

/** @internal @This purges the current table.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psii_sub_clean(struct upipe *upipe)
{
    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);
    struct uchain *uchain;
    ulist_delete_foreach (&upipe_ts_psii_sub->table, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        ulist_delete(&upipe_ts_psii_sub->table, uchain);
        uref_free(uref);
    }
}
/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_psii_sub_input(struct upipe *upipe, struct uref *uref,
                                    struct upump *upump)
{
    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);

    if (uref_block_get_start(uref) || ulist_empty(&upipe_ts_psii_sub->table)) {
        upipe_ts_psii_sub_clean(upipe);

        uint64_t dts;
        if (uref_clock_get_dts(uref, &dts)) {
            uint64_t dts_sys, delay = 0;
            uref_clock_get_vbv_delay(uref, &delay);
            upipe_ts_psii_sub->next_dts = dts - delay;
            if (uref_clock_get_dts_sys(uref, &dts_sys))
                upipe_ts_psii_sub->next_dts_sys = dts_sys - delay;
        } else {
            /* Trigger immediate insertion. */
            upipe_ts_psii_sub->next_dts = 0;
            upipe_ts_psii_sub->next_dts_sys = 0;
        }
    }

    ulist_add(&upipe_ts_psii_sub->table, uref_to_uchain(uref));
}

/** @internal @This outputs a PSI table.
 *
 * @param upipe description structure of the pipe
 * @param next_uref next uref to mux
 */
static void upipe_ts_psii_sub_output(struct upipe *upipe,
                                     struct uref *next_uref)
{
    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);

    uint64_t dts = upipe_ts_psii_sub->next_dts;
    uint64_t dts_sys = upipe_ts_psii_sub->next_dts_sys;

    if (unlikely(!dts)) {
        uref_clock_get_dts(next_uref, &dts);
        uref_clock_get_dts_sys(next_uref, &dts_sys);
    }

    upipe_ts_psii_sub->next_dts = dts + upipe_ts_psii_sub->interval;
    if (dts_sys != UINT64_MAX)
        upipe_ts_psii_sub->next_dts_sys = dts_sys + upipe_ts_psii_sub->interval;
    else
        upipe_ts_psii_sub->next_dts_sys = UINT64_MAX;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psii_sub->table, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        struct uref *output = uref_dup(uref);
        if (unlikely(output == NULL)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        uref_clock_set_dts(output, dts);
        if (dts_sys != UINT64_MAX)
            uref_clock_set_dts_sys(output, dts_sys);
        uref_clock_set_vbv_delay(output, DEFAULT_DELAY);
        upipe_input(upipe_ts_psii_sub->encaps, output, NULL);
    }
}

/** @internal @This returns the current interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return false in case of error
 */
static bool _upipe_ts_psii_sub_get_interval(struct upipe *upipe,
                                            uint64_t *interval_p)
{
    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_psii_sub->interval;
    return true;
}

/** @internal @This sets the interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return false in case of error
 */
static bool _upipe_ts_psii_sub_set_interval(struct upipe *upipe,
                                            uint64_t interval)
{
    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);
    int64_t diff = interval - upipe_ts_psii_sub->interval;
    upipe_ts_psii_sub->interval = interval;
    if (upipe_ts_psii_sub->next_dts != UINT64_MAX)
        upipe_ts_psii_sub->next_dts += diff;
    if (upipe_ts_psii_sub->next_dts_sys != UINT64_MAX)
        upipe_ts_psii_sub->next_dts_sys += diff;
    return true;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psii_sub_control(struct upipe *upipe,
                                      enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_TS_PSII_SUB_GET_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PSII_SUB_SIGNATURE);
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return _upipe_ts_psii_sub_get_interval(upipe, interval_p);
        }
        case UPIPE_TS_PSII_SUB_SET_INTERVAL: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PSII_SUB_SIGNATURE);
            uint64_t interval = va_arg(args, uint64_t);
            return _upipe_ts_psii_sub_set_interval(upipe, interval);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psii_sub_free(struct upipe *upipe)
{
    struct upipe_ts_psii_sub *upipe_ts_psii_sub =
        upipe_ts_psii_sub_from_upipe(upipe);
    struct upipe_ts_psii *upipe_ts_psii =
        upipe_ts_psii_from_sub_mgr(upipe->mgr);

    upipe_release(upipe_ts_psii_sub->encaps);
    upipe_throw_dead(upipe);

    upipe_ts_psii_sub_clean(upipe);
    upipe_ts_psii_sub_clean_sub(upipe);
    upipe_ts_psii_sub_free_flow(upipe);

    upipe_release(upipe_ts_psii_to_upipe(upipe_ts_psii));
}

/** @internal @This initializes the sub manager for a ts_psii pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psii_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_ts_psii *upipe_ts_psii = upipe_ts_psii_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_ts_psii->sub_mgr;
    sub_mgr->signature = UPIPE_TS_PSII_SUB_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_ts_psii_sub_alloc;
    sub_mgr->upipe_input = upipe_ts_psii_sub_input;
    sub_mgr->upipe_control = upipe_ts_psii_sub_control;
    sub_mgr->upipe_free = upipe_ts_psii_sub_free;
    sub_mgr->upipe_mgr_free = NULL;
}

/** @internal @This catches the need_uref_mgr and need_ubuf_mgr events from
 * subpipes.
 *
 * @param uprobe pointer to the probe in upipe_ts_psii
 * @param subpipe pointer to the subpipe
 * @param event event triggered by the subpipe
 * @param args arguments of the event
 * @return true if the event was caught
 */
static bool upipe_ts_psii_probe(struct uprobe *uprobe, struct upipe *subpipe,
                               enum uprobe_event event, va_list args)
{
    struct upipe_ts_psii *upipe_ts_psii =
        container_of(uprobe, struct upipe_ts_psii, probe);
    struct upipe *upipe = upipe_ts_psii_to_upipe(upipe_ts_psii);

    switch (event) {
        case UPROBE_NEED_UREF_MGR:
            if (upipe_ts_psii->uref_mgr == NULL)
                upipe_throw_need_uref_mgr(upipe);
            if (upipe_ts_psii->uref_mgr != NULL)
                upipe_set_uref_mgr(subpipe, upipe_ts_psii->uref_mgr);
            return true;
        case UPROBE_NEED_UBUF_MGR: {
            struct uref *flow_def = va_arg(args, struct uref *);
            if (upipe_ts_psii->ubuf_mgr == NULL)
                upipe_throw_need_ubuf_mgr(upipe, flow_def);
            if (upipe_ts_psii->ubuf_mgr != NULL)
                upipe_set_ubuf_mgr(subpipe, upipe_ts_psii->ubuf_mgr);
            return true;
        }
        default:
            return false;
    }
}

/** @internal @This allocates a ts_psii pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psii_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psii_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psii *upipe_ts_psii = upipe_ts_psii_from_upipe(upipe);
    upipe_ts_psii_init_uref_mgr(upipe);
    upipe_ts_psii_init_ubuf_mgr(upipe);
    upipe_ts_psii_init_output(upipe);
    upipe_ts_psii_init_sub_mgr(upipe);
    upipe_ts_psii_init_sub_subs(upipe);
    upipe_ts_psii_store_flow_def(upipe, flow_def);

    uprobe_init(&upipe_ts_psii->probe, upipe_ts_psii_probe, uprobe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This generates a PAT PSI section, using the uref received.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_psii_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_psii *upipe_ts_psii = upipe_ts_psii_from_upipe(upipe);

    uint64_t dts;
    if (unlikely(!uref_clock_get_dts(uref, &dts))) {
        upipe_warn(upipe, "non-dated packet received");
        uref_free(uref);
        return;
    }

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psii->subs, uchain) {
        struct upipe_ts_psii_sub *sub = upipe_ts_psii_sub_from_uchain(uchain);
        /* We only compare to DTS and do not use delay because DTS is the
         * latest time at which the incoming packet may be muxed, so if we
         * take into account delay the PSI section may arrive too late. */
        while (sub->next_dts <= dts)
            upipe_ts_psii_sub_output(upipe_ts_psii_sub_to_upipe(sub), uref);
    }

    upipe_ts_psii_output(upipe, uref, upump);
}

/** @internal @This sets the output on a pipe.
 *
 * @param upipe description structure of the pipe
 * @param output new output
 * @return false in case of error
 */
static bool _upipe_ts_psii_set_output(struct upipe *upipe, struct upipe *output)
{
    if (unlikely(!upipe_ts_psii_set_output(upipe, output)))
        return false;

    struct upipe_ts_psii *upipe_ts_psii = upipe_ts_psii_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psii->subs, uchain) {
        struct upipe_ts_psii_sub *sub = upipe_ts_psii_sub_from_uchain(uchain);
        if (likely(sub->encaps != NULL))
            upipe_set_output(sub->encaps, output);
    }
    return true;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psii_control(struct upipe *upipe,
                                  enum upipe_command command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_ts_psii_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_ts_psii_set_uref_mgr(upipe, uref_mgr);
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_ts_psii_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_ts_psii_set_ubuf_mgr(upipe, ubuf_mgr);
        }

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psii_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psii_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return _upipe_ts_psii_set_output(upipe, output);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psii_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_ts_psii_clean_sub_subs(upipe);
    upipe_ts_psii_clean_output(upipe);
    upipe_ts_psii_clean_ubuf_mgr(upipe);
    upipe_ts_psii_clean_uref_mgr(upipe);
    upipe_ts_psii_free_flow(upipe);
}

/** @This frees a upipe manager.
 *
 * @param mgr pointer to manager
 */
static void upipe_ts_psii_mgr_free(struct upipe_mgr *mgr)
{
    struct upipe_ts_psii_mgr *ts_psii_mgr =
        upipe_ts_psii_mgr_from_upipe_mgr(mgr);
    if (ts_psii_mgr->ts_encaps_mgr != NULL)
        upipe_mgr_release(ts_psii_mgr->ts_encaps_mgr);

    urefcount_clean(&ts_psii_mgr->mgr.refcount);
    free(ts_psii_mgr);
}

/** @This returns the management structure for all ts_psii pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psii_mgr_alloc(void)
{
    struct upipe_ts_psii_mgr *ts_psii_mgr =
        malloc(sizeof(struct upipe_ts_psii_mgr));
    if (unlikely(ts_psii_mgr == NULL))
        return NULL;

    ts_psii_mgr->ts_encaps_mgr = upipe_ts_encaps_mgr_alloc();

    ts_psii_mgr->mgr.signature = UPIPE_TS_PSII_SIGNATURE;
    ts_psii_mgr->mgr.upipe_alloc = upipe_ts_psii_alloc;
    ts_psii_mgr->mgr.upipe_input = upipe_ts_psii_input;
    ts_psii_mgr->mgr.upipe_control = upipe_ts_psii_control;
    ts_psii_mgr->mgr.upipe_free = upipe_ts_psii_free;
    ts_psii_mgr->mgr.upipe_mgr_free = upipe_ts_psii_mgr_free;
    urefcount_init(&ts_psii_mgr->mgr.refcount);
    return upipe_ts_psii_mgr_to_upipe_mgr(ts_psii_mgr);
}

/** @This processes control commands on a ts_psii manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_psii_mgr_control_va(struct upipe_mgr *mgr,
                                  enum upipe_ts_mux_mgr_command command,
                                  va_list args)
{
    struct upipe_ts_psii_mgr *ts_psii_mgr = upipe_ts_psii_mgr_from_upipe_mgr(mgr);
    assert(urefcount_single(&ts_psii_mgr->mgr.refcount));

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_TS_MUX_MGR_GET_##NAME##_MGR: {                           \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = ts_psii_mgr->name##_mgr;                                   \
            return true;                                                    \
        }                                                                   \
        case UPIPE_TS_MUX_MGR_SET_##NAME##_MGR: {                           \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            if (ts_psii_mgr->name##_mgr != NULL)                            \
                upipe_mgr_release(ts_psii_mgr->name##_mgr);                 \
            if (m != NULL)                                                  \
                upipe_mgr_use(m);                                           \
            ts_psii_mgr->name##_mgr = m;                                    \
            return true;                                                    \
        }

        GET_SET_MGR(ts_encaps, TS_ENCAPS)
#undef GET_SET_MGR

        default:
            return false;
    }
}

/** @This processes control commands on a ts_psii manager. This may only be
 * called before any pipe has been allocated.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
bool upipe_ts_psii_mgr_control(struct upipe_mgr *mgr,
                               enum upipe_ts_mux_mgr_command command, ...)
{
    va_list args;
    va_start(args, command);
    bool ret = upipe_ts_psii_mgr_control_va(mgr, command, args);
    va_end(args);
    return ret;
}

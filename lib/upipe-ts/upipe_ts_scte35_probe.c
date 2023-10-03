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
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_uclock.h"
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
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;

    /** uclock structure */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** list of events */
    struct uchain events;
    /** list of signals */
    struct uchain signals;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_scte35p, upipe, UPIPE_TS_SCTE35P_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte35p, urefcount, upipe_ts_scte35p_free)
UPIPE_HELPER_VOID(upipe_ts_scte35p)
UPIPE_HELPER_UPUMP_MGR(upipe_ts_scte35p, upump_mgr)
UPIPE_HELPER_UCLOCK(upipe_ts_scte35p, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

/** @internal @This describes an SCTE-35 event. */
struct upipe_ts_scte35p_event {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** event ID */
    uint64_t event_id;
    /** uref describing the event */
    struct uref *uref;
    /** upump which will trigger the event */
    struct upump *upump;
    /** first debug message? */
    bool first_debug;

    /** pointer to public upipe structure */
    struct upipe *upipe;
};

UBASE_FROM_TO(upipe_ts_scte35p_event, uchain, uchain, uchain)

static void upipe_ts_scte35p_event_trigger(struct upipe *upipe,
        struct upipe_ts_scte35p_event *event, uint64_t skew);

/** @internal @This describes an SCTE-35 time signal. */
struct upipe_ts_scte35p_signal {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pts orig of the signal */
    uint64_t pts_orig;
    /** pts of the signal */
    uint64_t pts;
    /** uref describing the events */
    struct uref *uref;
    /** upump which will trigger the event */
    struct upump *upump;
    /** first debug message? */
    bool first_debug;

    /** pointer to public upipe structure */
    struct upipe *upipe;
};

UBASE_FROM_TO(upipe_ts_scte35p_signal, uchain, uchain, uchain)

static void upipe_ts_scte35p_signal_trigger(struct upipe *upipe,
        struct upipe_ts_scte35p_signal *signal, uint64_t skew);

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
    upipe_ts_scte35p_init_upump_mgr(upipe);
    upipe_ts_scte35p_init_uclock(upipe);
    ulist_init(&upipe_ts_scte35p->events);
    ulist_init(&upipe_ts_scte35p->signals);
    upipe_throw_ready(upipe);
    upipe_ts_scte35p_check_upump_mgr(upipe);
    upipe_ts_scte35p_require_uclock(upipe);
    return upipe;
}

/** @internal @This finds or allocates an SCTE35 event.
 *
 * @param upipe description structure of the pipe
 * @param event_id event ID
 * @param uref uref describing the event
 * @return the SCTE35 event
 */
static struct upipe_ts_scte35p_event *
    upipe_ts_scte35p_event_find(struct upipe *upipe, uint64_t event_id,
                                struct uref *uref)
{
    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_scte35p->events, uchain) {
        struct upipe_ts_scte35p_event *event =
            upipe_ts_scte35p_event_from_uchain(uchain);
        if (event->event_id == event_id) {
            uref_free(event->uref);
            event->uref = uref;
            if (event->upump != NULL) {
                upump_stop(event->upump);
                upump_free(event->upump);
                event->upump = NULL;
            }
            return event;
        }
    }

    struct upipe_ts_scte35p_event *event =
        malloc(sizeof(struct upipe_ts_scte35p_event));
    if (unlikely(event == NULL))
        return NULL;

    uchain_init(&event->uchain);
    event->first_debug = true;
    event->event_id = event_id;
    event->uref = uref;
    event->upump = NULL;
    event->upipe = upipe;
    ulist_add(&upipe_ts_scte35p->events,
              upipe_ts_scte35p_event_to_uchain(event));
    return event;
}

/** @internal @This frees an SCTE35 event.
 *
 * @param upipe description structure of the pipe
 * @param event description structure of the event
 */
static void upipe_ts_scte35p_event_free(struct upipe *upipe,
        struct upipe_ts_scte35p_event *event)
{
    uref_free(event->uref);
    if (event->upump != NULL) {
        upump_stop(event->upump);
        upump_free(event->upump);
    }
    ulist_delete(upipe_ts_scte35p_event_to_uchain(event));
    free(event);
}

/** @internal @This is called when an SCTE35 event triggers.
 *
 * @param upump description structure of the watcher
 */
static void upipe_ts_scte35p_event_watcher(struct upump *upump)
{
    struct upipe_ts_scte35p_event *event =
        upump_get_opaque(upump, struct upipe_ts_scte35p_event *);
    struct upipe *upipe = event->upipe;
    upump_stop(upump);
    upump_free(upump);
    event->upump = NULL;
    upipe_ts_scte35p_event_trigger(upipe, event, 0);
}

/** @internal @This waits for an SCTE35 event.
 *
 * @param upipe description structure of the pipe
 * @param event description structure of the event
 * @param timeout time after which the event triggers
 */
static void upipe_ts_scte35p_event_wait(struct upipe *upipe,
        struct upipe_ts_scte35p_event *event, uint64_t timeout)
{
    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);

    if (event->first_debug)
        upipe_dbg_va(upipe, "splice %"PRIu64" waiting %"PRIu64" ms",
                     event->event_id, timeout * 1000 / UCLOCK_FREQ);
    else
        upipe_verbose_va(upipe, "splice %"PRIu64" waiting %"PRIu64" ms",
                         event->event_id, timeout * 1000 / UCLOCK_FREQ);
    event->first_debug = false;

    struct upump *watcher = upump_alloc_timer(upipe_ts_scte35p->upump_mgr,
            upipe_ts_scte35p_event_watcher, event, upipe->refcount, timeout, 0);
    if (unlikely(watcher == NULL)) {
        upipe_ts_scte35p_event_free(upipe, event);
        upipe_err(upipe, "can't create watcher");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }

    event->upump = watcher;
    upump_start(watcher);
}

/** @internal @This triggers an SCTE35 event and handles its duration.
 *
 * @param upipe description structure of the pipe
 * @param event description structure of the event
 * @param skew skew of the trigger
 */
static void upipe_ts_scte35p_event_trigger(struct upipe *upipe,
        struct upipe_ts_scte35p_event *event, uint64_t skew)
{
    upipe_dbg_va(upipe, "throw ts_scte35p_event %"PRIu64, event->event_id);
    upipe_throw(upipe, UPROBE_TS_SCTE35P_EVENT, UPIPE_TS_SCTE35P_SIGNATURE,
                event->uref);

    uint64_t duration;
    if (!ubase_check(uref_ts_scte35_get_auto_return(event->uref)) ||
        !ubase_check(uref_clock_get_duration(event->uref, &duration))) {
        upipe_ts_scte35p_event_free(upipe, event);
        return;
    }
    uref_clock_delete_duration(event->uref);

    if (ubase_check(uref_ts_scte35_get_out_of_network(event->uref)))
        uref_ts_scte35_delete_out_of_network(event->uref);
    else
        uref_ts_scte35_set_out_of_network(event->uref);

    if (duration <= skew) {
        upipe_warn_va(upipe, "splice return %"PRIu64" in the past (%"PRIu64")",
                      event->event_id, skew - duration);
        upipe_ts_scte35p_event_trigger(upipe, event, skew - duration);
        return;
    }

    upipe_ts_scte35p_event_wait(upipe, event, duration - skew);
}

/** @internal @This finds or allocates an SCTE35 signal.
 *
 * @param upipe description structure of the pipe
 * @param uref signal description
 * @return the SCTE35 signal
 */
static struct upipe_ts_scte35p_signal *
upipe_ts_scte35p_signal_find(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);

    uint64_t pts_orig;
    uint64_t pts;
    if (unlikely(!ubase_check(uref_clock_get_pts_orig(uref, &pts_orig)) ||
                 !ubase_check(uref_clock_get_pts_sys(uref, &pts))))
        return NULL;

    struct uchain *uchain;
    ulist_foreach(&upipe_ts_scte35p->signals, uchain) {
        struct upipe_ts_scte35p_signal *signal =
            upipe_ts_scte35p_signal_from_uchain(uchain);

        if (signal->pts_orig != pts_orig)
            continue;

        /* update signal pts with last pts */
        signal->pts = pts;

        uint64_t old_nb = 0;
        uref_ts_flow_get_descriptors(signal->uref, &old_nb);
        for (uint64_t i = 0; i < old_nb; i++) {
            const uint8_t *desc = NULL;
            size_t len = 0;
            bool found = false;

            uref_ts_scte35_desc_get_seg(signal->uref, &desc, &len, i);
            if (!desc)
                continue;

            uint32_t event_id = scte35_seg_desc_get_event_id(desc);

            uint64_t nb = 0;
            uref_ts_flow_get_descriptors(uref, &nb);
            for (uint64_t j = 0; !found && j < nb; j++) {
                const uint8_t *d = NULL;
                size_t l = 0;
                uref_ts_scte35_desc_get_seg(uref, &d, &l, j);
                if (!d || scte35_seg_desc_get_event_id(d) != event_id)
                    continue;
                found = true;
            }

            if (!found)
                uref_ts_flow_add_descriptor(uref, desc, len);
        }

        uref_free(signal->uref);
        signal->uref = uref;

        return signal;
    }

    struct upipe_ts_scte35p_signal *signal = malloc(sizeof (*signal));
    if (!signal)
        return NULL;

    signal->first_debug = true;
    signal->pts_orig = pts_orig;
    signal->pts = pts;
    signal->upipe = upipe;
    signal->upump = NULL;
    signal->uref = uref;
    ulist_add(&upipe_ts_scte35p->signals, &signal->uchain);
    return signal;
}

/** @internal @This frees a SCTE35 signal.
 *
 * @param signal SCTE35 signal to free
 */
static void upipe_ts_scte35p_signal_free(struct upipe_ts_scte35p_signal *signal)
{
    uref_free(signal->uref);
    if (signal->upump != NULL) {
        upump_stop(signal->upump);
        upump_free(signal->upump);
    }
    ulist_delete(upipe_ts_scte35p_signal_to_uchain(signal));
    free(signal);
}

/** @internal @This is called when an SCTE35 signal triggers.
 *
 * @param upump description structure of the watcher
 */
static void upipe_ts_scte35p_signal_watcher(struct upump *upump)
{
    struct upipe_ts_scte35p_signal *signal =
        upump_get_opaque(upump, struct upipe_ts_scte35p_signal *);
    struct upipe *upipe = signal->upipe;
    upump_stop(upump);
    upump_free(upump);
    signal->upump = NULL;
    upipe_ts_scte35p_signal_trigger(upipe, signal, 0);
}

/** @internal @This waits for an SCTE35 signal.
 *
 * @param upipe description structure of the pipe
 * @param signal description structure of the signal
 * @param timeout time after which the signal triggers
 */
static void upipe_ts_scte35p_signal_wait(struct upipe *upipe,
                                         struct upipe_ts_scte35p_signal *signal,
                                         uint64_t timeout)
{
    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);

    if (signal->first_debug)
        upipe_dbg_va(upipe, "signal waiting %"PRIu64" ms",
                     timeout * 1000 / UCLOCK_FREQ);
    else
        upipe_verbose_va(upipe, "signal waiting %"PRIu64" ms",
                         timeout * 1000 / UCLOCK_FREQ);
    signal->first_debug = false;

    if (signal->upump) {
        upump_stop(signal->upump);
        upump_free(signal->upump);
    }
    signal->upump = NULL;

    struct upump *watcher = upump_alloc_timer(upipe_ts_scte35p->upump_mgr,
            upipe_ts_scte35p_signal_watcher,
            signal, upipe->refcount, timeout, 0);
    if (unlikely(watcher == NULL)) {
        upipe_ts_scte35p_signal_free(signal);
        upipe_err(upipe, "can't create watcher");
        upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
        return;
    }

    signal->upump = watcher;
    upump_start(watcher);
}

/** @internal @This triggers an SCTE35 signal and handles its duration.
 *
 * @param upipe description structure of the pipe
 * @param signal description structure of the signal
 * @param skew skew of the trigger
 */
static void upipe_ts_scte35p_signal_trigger(struct upipe *upipe,
        struct upipe_ts_scte35p_signal *signal, uint64_t skew)
{
    upipe_dbg_va(upipe, "throw ts_scte35p_signal");
    upipe_throw(upipe, UPROBE_TS_SCTE35P_SIGNAL,
                UPIPE_TS_SCTE35P_SIGNATURE, signal->uref);
    upipe_ts_scte35p_signal_free(signal);
}

/** @internal @This handles a null event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35p_input_null(struct upipe *upipe,
                                        struct uref *uref,
                                        struct upump **upump_p)
{
    upipe_verbose_va(upipe, "throw ts_scte35p_null");
    upipe_throw(upipe, UPROBE_TS_SCTE35P_NULL, UPIPE_TS_SCTE35P_SIGNATURE,
                uref);
    uref_free(uref);
}

/** @internal @This handles a time signal event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35p_input_time_signal(struct upipe *upipe,
                                               struct uref *uref,
                                               struct upump **upump_p)
{
    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);

    struct upipe_ts_scte35p_signal *signal =
        upipe_ts_scte35p_signal_find(upipe, uref);
    if (unlikely(signal == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    upipe_ts_scte35p_check_upump_mgr(upipe);
    if (unlikely(upipe_ts_scte35p->upump_mgr == NULL)) {
        upipe_ts_scte35p_signal_free(signal);
        upipe_warn(upipe, "no upump manager");
        return;
    }

    if (unlikely(upipe_ts_scte35p->uclock == NULL)) {
        upipe_ts_scte35p_require_uclock(upipe);
        if (unlikely(upipe_ts_scte35p->uclock == NULL)) {
            upipe_ts_scte35p_signal_free(signal);
            upipe_warn(upipe, "no uclock");
            return;
        }
    }

    uint64_t now = uclock_now(upipe_ts_scte35p->uclock);
    uint64_t timeout = 0;
    if (signal->pts <= now)
        upipe_warn_va(upipe, "signal in the past (%"PRIu64")",
                      now - signal->pts);
    else
        timeout = signal->pts - now;

    upipe_ts_scte35p_signal_wait(upipe, signal, timeout);
}

/** @internal @This handles an insert event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35p_input_insert(struct upipe *upipe,
                                          struct uref *uref,
                                          struct upump **upump_p)
{
    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);

    uint64_t event_id;
    if (!ubase_check(uref_ts_scte35_get_event_id(uref, &event_id))) {
        uref_free(uref);
        upipe_warn(upipe, "invalid SCTE35 meta received");
        return;
    }

    struct upipe_ts_scte35p_event *event =
        upipe_ts_scte35p_event_find(upipe, event_id, uref);
    if (unlikely(event == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    if (ubase_check(uref_ts_scte35_get_cancel(uref))) {
        upipe_dbg_va(upipe, "splice %"PRIu64" cancelled", event_id);
        upipe_ts_scte35p_event_free(upipe, event);
        return;
    }

    uint64_t pts;
    if (!ubase_check(uref_clock_get_pts_sys(uref, &pts))) {
        upipe_dbg_va(upipe, "splice %"PRIu64" immediate", event_id);
        upipe_ts_scte35p_event_trigger(upipe, event, 0);
        return;
    }

    upipe_ts_scte35p_check_upump_mgr(upipe);
    if (unlikely(upipe_ts_scte35p->upump_mgr == NULL)) {
        upipe_ts_scte35p_event_free(upipe, event);
        upipe_warn(upipe, "no upump manager");
        return;
    }

    if (unlikely(upipe_ts_scte35p->uclock == NULL)) {
        upipe_ts_scte35p_require_uclock(upipe);
        if (unlikely(upipe_ts_scte35p->uclock == NULL)) {
            upipe_ts_scte35p_event_free(upipe, event);
            upipe_warn(upipe, "no uclock");
            return;
        }
    }

    uint64_t now = uclock_now(upipe_ts_scte35p->uclock);
    if (pts <= now) {
        upipe_warn_va(upipe, "splice %"PRIu64" in the past (%"PRIu64")",
                      event_id, now - pts);
        upipe_ts_scte35p_event_trigger(upipe, event, now - pts);
        return;
    }

    upipe_ts_scte35p_event_wait(upipe, event, pts - now);
}

/** @internal @This handles an SCTE35 meta.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35p_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    /* ignore empty uref */
    if (uref->udict == NULL) {
        uref_free(uref);
        return;
    }

    uint8_t command_type;
    if (!ubase_check(uref_ts_scte35_get_command_type(uref, &command_type))) {
        uref_free(uref);
        upipe_warn(upipe, "no SCTE35 command type");
        return;
    }
    switch (command_type) {
        case SCTE35_NULL_COMMAND:
            return upipe_ts_scte35p_input_null(upipe, uref, upump_p);
        case SCTE35_INSERT_COMMAND:
            return upipe_ts_scte35p_input_insert(upipe, uref, upump_p);
        case SCTE35_TIME_SIGNAL_COMMAND:
            return upipe_ts_scte35p_input_time_signal(upipe, uref, upump_p);
        default:
            upipe_dbg_va(upipe, "unsupported command type %u", command_type);
            uref_free(uref);
            return;
    }
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_scte35p_set_flow_def(struct upipe *upipe,
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
static int upipe_ts_scte35p_control(struct upipe *upipe,
                                    int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);

        case UPIPE_ATTACH_UPUMP_MGR:
            return upipe_ts_scte35p_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_ts_scte35p_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_scte35p_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35p_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_scte35p *upipe_ts_scte35p =
        upipe_ts_scte35p_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_scte35p->events, uchain, uchain_tmp) {
        struct upipe_ts_scte35p_event *event =
            upipe_ts_scte35p_event_from_uchain(uchain);
        upipe_ts_scte35p_event_free(upipe, event);
    }
    ulist_delete_foreach (&upipe_ts_scte35p->signals, uchain, uchain_tmp) {
        struct upipe_ts_scte35p_signal *signal =
            upipe_ts_scte35p_signal_from_uchain(uchain);
        upipe_ts_scte35p_signal_free(signal);
    }

    upipe_ts_scte35p_clean_upump_mgr(upipe);
    upipe_ts_scte35p_clean_uclock(upipe);
    upipe_ts_scte35p_clean_urefcount(upipe);
    upipe_ts_scte35p_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte35p_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE35P_SIGNATURE,

    .upipe_alloc = upipe_ts_scte35p_alloc,
    .upipe_input = upipe_ts_scte35p_input,
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

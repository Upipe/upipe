/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-ts/upipe_ts_scte35_probe.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/uref_ts_scte35.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

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

    /** pointer to public upipe structure */
    struct upipe *upipe;
};

UBASE_FROM_TO(upipe_ts_scte35p_event, uchain, uchain, uchain)

static void upipe_ts_scte35p_event_trigger(struct upipe *upipe,
        struct upipe_ts_scte35p_event *event, uint64_t skew);

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
    upipe_dbg_va(upipe, "splice %"PRIu64" waiting %"PRIu64" ms",
                 event->event_id, timeout * 1000 / UCLOCK_FREQ);

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

/** @internal @This handles an SCTE35 meta.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35p_input(struct upipe *upipe, struct uref *uref,
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
        uref_free(uref);
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
        case UPIPE_ATTACH_UPUMP_MGR:
            return upipe_ts_scte35p_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_ts_scte35p_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
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

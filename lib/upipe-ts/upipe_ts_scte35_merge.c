/*
 * Copyright (C) 2022 EasyTools
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe module merging the SCTE-35 events
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
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe-ts/upipe_ts_scte35_merge.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe-ts/uref_ts_scte35.h"
#include "upipe-ts/uref_ts_scte35_desc.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include <bitstream/scte/35.h>

/** we only accept SCTE 35 metadata */
#define EXPECTED_FLOW_DEF "void.scte35."

/** @internal @This is the private context of a ts_scte35m pipe. */
struct upipe_ts_scte35m {
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** upump */
    struct upump *upump;

    /** uclock structure */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** list of events */
    struct uchain events;
    /** list of descriptors */
    struct uchain descriptors;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_scte35m, upipe, UPIPE_TS_SCTE35M_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_ts_scte35m, urefcount, upipe_ts_scte35m_free);
UPIPE_HELPER_VOID(upipe_ts_scte35m);
UPIPE_HELPER_UPUMP_MGR(upipe_ts_scte35m, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_ts_scte35m, upump, upump_mgr);
UPIPE_HELPER_UCLOCK(upipe_ts_scte35m, uclock, uclock_request, NULL,
                    upipe_throw_provide_request, NULL);

/** @internal @This throws an event when a SCTE-35 event is created or modified.
 *
 * @param upipe description structure of the pipe
 * @param uref describing the old event
 * @param uref describing the new event
 * @return an error code
 */
static int upipe_ts_scte35m_throw_changed(struct upipe *upipe,
                                          struct uref *old_uref,
                                          struct uref *uref)
{
    upipe_dbg_va(upipe, "throw changed %p -> %p", old_uref, uref);
    return upipe_throw(upipe, UPROBE_TS_SCTE35M_CHANGED,
                       UPIPE_TS_SCTE35M_SIGNATURE, old_uref, uref);
}

/** @internal @This throws an event when a SCTE-35 event expired.
 *
 * @param upipe description structure of the pipe
 * @param uref uref describing the event
 * @return an error code
 */
static int upipe_ts_scte35m_throw_expired(struct upipe *upipe,
                                          struct uref *uref)
{
    upipe_dbg_va(upipe, "throw expired %p", uref);
    return upipe_throw(upipe, UPROBE_TS_SCTE35M_EXPIRED,
                       UPIPE_TS_SCTE35M_SIGNATURE, uref);
}

/** @hidden */
static void upipe_ts_scte35m_expired(struct upump *upump);

/** @internal @This allocates a ts_scte35m pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_scte35m_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature, va_list args)
{
    struct upipe *upipe =
        upipe_ts_scte35m_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_scte35m *upipe_ts_scte35m =
        upipe_ts_scte35m_from_upipe(upipe);
    upipe_ts_scte35m_init_urefcount(upipe);
    upipe_ts_scte35m_init_upump_mgr(upipe);
    upipe_ts_scte35m_init_upump(upipe);
    upipe_ts_scte35m_init_uclock(upipe);
    ulist_init(&upipe_ts_scte35m->events);
    ulist_init(&upipe_ts_scte35m->descriptors);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This waits for the next event expiration.
 *
 * @param upipe description structure of the pipe
 * @param now current clock value or UINT64_MAX
 */
static void upipe_ts_scte35m_wait(struct upipe *upipe, uint64_t now)
{
    struct upipe_ts_scte35m *upipe_ts_scte35m =
        upipe_ts_scte35m_from_upipe(upipe);

    uint64_t timeout = UINT64_MAX;
    struct uchain *uchain;
    ulist_foreach(&upipe_ts_scte35m->events, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t pts = UINT64_MAX;
        uref_clock_get_pts_sys(uref, &pts);
        if (pts == UINT64_MAX || pts < now)
            timeout = 0;
        else if (pts - now < timeout)
            timeout = pts - now;
    }

    if (timeout == UINT64_MAX || !upipe_ts_scte35m->upump_mgr)
        return;

    if (timeout)
        upipe_verbose_va(upipe, "wait next expiration in %.3f ms",
                     timeout * 1000. / UCLOCK_FREQ);
    else
        upipe_verbose_va(upipe, "expired immediately");
    upipe_ts_scte35m_wait_upump(upipe, timeout, upipe_ts_scte35m_expired);
}

/** @internal @This removes expired events.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35m_update(struct upipe *upipe)
{
    struct upipe_ts_scte35m *upipe_ts_scte35m =
        upipe_ts_scte35m_from_upipe(upipe);

    uint64_t now = upipe_ts_scte35m_now(upipe);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_ts_scte35m->events, uchain, uchain_tmp) {
        struct uref *uref = uref_from_uchain(uchain);
        uint64_t pts = UINT64_MAX;
        uref_clock_get_pts_sys(uref, &pts);

        if (now == UINT64_MAX || pts == UINT64_MAX || pts < now) {
            upipe_ts_scte35m_throw_expired(upipe, uref);

            uint64_t duration;
            if (!ubase_check(uref_ts_scte35_get_auto_return(uref)) ||
                !ubase_check(uref_clock_get_duration(uref, &duration))) {
                ulist_delete(uchain);
                uref_free(uref);
            } else {
                uref_clock_delete_duration(uref);
                uref_ts_scte35_delete_auto_return(uref);
                if (ubase_check(uref_ts_scte35_get_out_of_network(uref)))
                    uref_ts_scte35_delete_out_of_network(uref);
                else
                    uref_ts_scte35_set_out_of_network(uref);
                if (pts != UINT64_MAX)
                    uref_clock_set_pts_sys(uref, pts + duration);
                else
                    uref_clock_set_pts_sys(uref, now + duration);
            }
        }
    }

    upipe_ts_scte35m_wait(upipe, now);
}

/** @internal @This returns the descriptor pointer if it's a splice descriptor,
 * otherwise NULL.
 *
 * @param desc pointer to a descriptor
 * @return desc if it is a splice descriptor, NULL otherwise
 */
static const uint8_t *scte35_splice_desc(const uint8_t *desc)
{
    uint8_t len = desc_get_length(desc);
    if (len < SCTE35_SPLICE_DESC_HEADER_SIZE - DESC_HEADER_SIZE)
        return NULL;
    if (scte35_splice_desc_get_identifier(desc) !=
        SCTE35_SPLICE_DESC_IDENTIFIER)
        return NULL;
    return desc;
}

/** @internal @This returns the descriptor pointer if it's a splice
 * segmentation descriptor.
 *
 * @param desc pointer to a descriptor
 * @return desc if it is a splice descriptor, NULL otherwise
 */
static const uint8_t *scte35_splice_desc_seg(const uint8_t *desc)
{
    if (scte35_splice_desc(desc) &&
        scte35_splice_desc_get_tag(desc) == SCTE35_SPLICE_DESC_TAG_SEG)
        return desc;
    return NULL;
}

/** @internal @This returns the previous event if any.
 *
 * @param upipe description structure of the pipe
 * @param uref event description to look for
 * @return the previous event or NULL
 */
static struct uref *upipe_ts_scte35m_update_desc(struct upipe *upipe,
                                                 struct uref *uref)
{
    struct upipe_ts_scte35m *upipe_ts_scte35m =
        upipe_ts_scte35m_from_upipe(upipe);


    uint64_t descriptors = 0;
    uref_ts_flow_get_descriptors(uref, &descriptors);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach(&upipe_ts_scte35m->events, uchain, uchain_tmp) {
        struct uref *prev = uref_from_uchain(uchain);

        if (uref_ts_scte35_cmp_command_type(uref, prev))
            continue;

        uint64_t nb = 0;
        uref_ts_flow_get_descriptors(uref, &nb);
        uint64_t changes = 0;

        for (unsigned j = 0; j < nb; j++) {
            const uint8_t *d = NULL;
            size_t l = 0;
            uref_ts_flow_get_descriptor(prev, &d, &l, j);
            if (l < DESC_HEADER_SIZE || !scte35_splice_desc_seg(d))
                continue;

            uint32_t event_id = scte35_seg_desc_get_event_id(d);

            for (unsigned i = 0; i < descriptors; i++) {
                const uint8_t *desc = NULL;
                size_t len = 0;
                uref_ts_flow_get_descriptor(uref, &desc, &len, i);
                if (len < DESC_HEADER_SIZE || !scte35_splice_desc_seg(desc) ||
                    event_id != scte35_seg_desc_get_event_id(desc))
                    continue;

                upipe_verbose(upipe, "update previous descriptor");
                uref_ts_flow_remove_descriptor(prev, j);
                changes++;
                j--;
                nb--;
                break;
            }
        }

        if (changes)
            upipe_ts_scte35m_throw_changed(upipe, prev, prev);
    }

    return NULL;
}

/** @internal @This returns the previous received message match this uref.
 *
 * @param upipe description structure of the pipe
 * @param uref message to look for
 * @return the previous received uref or NULL
 */
static struct uref *upipe_ts_scte35m_find_event(struct upipe *upipe,
                                                struct uref *uref)
{
    struct upipe_ts_scte35m *scte35m = upipe_ts_scte35m_from_upipe(upipe);

    uint8_t type;
    if (unlikely(!ubase_check(uref_ts_scte35_get_command_type(uref, &type))))
        return NULL;

    struct uchain *uchain;
    ulist_foreach(&scte35m->events, uchain) {
        struct uref *prev = uref_from_uchain(uchain);
        if (uref_ts_scte35_cmp_command_type(uref, prev))
            continue;

        switch (type) {
            case SCTE35_INSERT_COMMAND:
                if (!uref_ts_scte35_cmp_event_id(uref, prev))
                    return prev;
                break;

            case SCTE35_TIME_SIGNAL_COMMAND: {
                uint64_t pts_orig = 0, prev_pts_orig = 0;
                uref_clock_get_pts_orig(uref, &pts_orig);
                uref_clock_get_pts_orig(prev, &prev_pts_orig);
                if (pts_orig == prev_pts_orig)
                    return prev;
                break;
            }
        }
    }
    return NULL;
}

/** @internal @This handles an SCTE35 meta.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_scte35m_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_ts_scte35m *scte35m = upipe_ts_scte35m_from_upipe(upipe);

    /* ignore empty uref */
    if (uref->udict == NULL) {
        uref_free(uref);
        return;
    }

    uint8_t type;
    if (!ubase_check(uref_ts_scte35_get_command_type(uref, &type))) {
        upipe_warn(upipe, "invalid SCTE-35 event");
        uref_free(uref);
        return;
    }

    if (type == SCTE35_NULL_COMMAND) {
        uref_clock_delete_date_orig(uref);
        uref_clock_delete_date_prog(uref);
        uref_clock_delete_date_sys(uref);
    }

    upipe_ts_scte35m_update_desc(upipe, uref);

    struct uref *prev = upipe_ts_scte35m_find_event(upipe, uref);
    if (prev) {
        upipe_verbose(upipe, "update previous message");
        uint64_t nb = 0;
        uref_ts_flow_get_descriptors(prev, &nb);
        for (unsigned i = 0; i < nb; i++) {
            const uint8_t *d = NULL;
            size_t l = 0;
            uref_ts_flow_get_descriptor(prev, &d, &l, i);
            if (d && l)
                uref_ts_flow_add_descriptor(uref, d, l);
        }
        upipe_ts_scte35m_throw_changed(upipe, prev, uref);
        ulist_delete(uref_to_uchain(prev));
        uref_free(prev);
    }
    else
        upipe_ts_scte35m_throw_changed(upipe, NULL, uref);
    ulist_add(&scte35m->events, uref_to_uchain(uref));
    upipe_ts_scte35m_update(upipe);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_scte35m_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    return UBASE_ERR_NONE;
}

/** @internal @This is called by the expiration pump.
 *
 * @param upump description structure of the watcher
 */
static void upipe_ts_scte35m_expired(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_ts_scte35m_update(upipe);
}

/** @internal @This checks the internal state of the pipe.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_ts_scte35m_check(struct upipe *upipe)
{
    struct upipe_ts_scte35m *upipe_ts_scte35m =
        upipe_ts_scte35m_from_upipe(upipe);

    if (unlikely(!upipe_ts_scte35m->uclock))
        upipe_ts_scte35m_require_uclock(upipe);

    upipe_ts_scte35m_wait(upipe, upipe_ts_scte35m_now(upipe));

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35m_control_real(struct upipe *upipe,
                                         int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);

        case UPIPE_ATTACH_UPUMP_MGR:
            return upipe_ts_scte35m_attach_upump_mgr(upipe);
        case UPIPE_ATTACH_UCLOCK:
            upipe_ts_scte35m_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_FLUSH:
            upipe_ts_scte35m_update(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_scte35m_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands and check internal state.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_scte35m_control(struct upipe *upipe,
                                    int command, va_list args)
{
    UBASE_RETURN(upipe_ts_scte35m_control_real(upipe, command, args));
    return upipe_ts_scte35m_check(upipe);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_scte35m_free(struct upipe *upipe)
{
    struct upipe_ts_scte35m *upipe_ts_scte35m =
        upipe_ts_scte35m_from_upipe(upipe);

    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_ts_scte35m->events)))
        uref_free(uref_from_uchain(uchain));

    upipe_ts_scte35m_clean_uclock(upipe);
    upipe_ts_scte35m_clean_upump(upipe);
    upipe_ts_scte35m_clean_upump_mgr(upipe);
    upipe_ts_scte35m_clean_urefcount(upipe);
    upipe_ts_scte35m_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_scte35m_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SCTE35M_SIGNATURE,

    .upipe_alloc = upipe_ts_scte35m_alloc,
    .upipe_input = upipe_ts_scte35m_input,
    .upipe_control = upipe_ts_scte35m_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_scte35m pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35m_mgr_alloc(void)
{
    return &upipe_ts_scte35m_mgr;
}

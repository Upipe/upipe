/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short implementation of a Upipe event loop using libev
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/upump.h>
#include <upipe/upump_common.h>
#include <upump-ev/upump_ev.h>

#include <stdlib.h>

#include <ev.h>

/** @This stores management parameters and local structures.
 */
struct upump_ev_mgr {
    /** ev private structure */
    struct ev_loop *ev_loop;

    /** refcount management structure */
    urefcount refcount;
    /** common structure */
    struct upump_common_mgr common_mgr;
};

/** @This stores local structures.
 */
struct upump_ev {
    /** true for a source watcher */
    bool source;
    /** type of event to watch */
    enum upump_watcher event;
    /** ev private structure */
    union {
        struct ev_io ev_io;
        struct ev_timer ev_timer;
        struct ev_idle ev_idle;
    };

    /** common structure */
    struct upump upump;
};

/** @internal @This returns the high-level upump_mgr structure.
 *
 * @param ev_mgr pointer to the upump_ev_mgr structure
 * @return pointer to the upump_mgr structure
 */
static inline struct upump_mgr *upump_ev_mgr_to_upump_mgr(struct upump_ev_mgr *ev_mgr)
{
    return upump_common_mgr_to_upump_mgr(&ev_mgr->common_mgr);
}

/** @internal @This returns the private upump_ev_mgr structure.
 *
 * @param mgr pointer to the upump_mgr structure
 * @return pointer to the upump_ev_mgr structure
 */
static inline struct upump_ev_mgr *upump_ev_mgr_from_upump_mgr(struct upump_mgr *mgr)
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    return container_of(common_mgr, struct upump_ev_mgr, common_mgr);
}

/** @internal @This returns the high-level upump structure.
 *
 * @param ev pointer to the upump_ev structure
 * @return pointer to the upump structure
 */
static inline struct upump *upump_ev_to_upump(struct upump_ev *upump_ev)
{
    return &upump_ev->upump;
}

/** @internal @This returns the private upump_ev structure.
 *
 * @param mgr pointer to the upump structure
 * @return pointer to the upump_ev structure
 */
static inline struct upump_ev *upump_ev_from_upump(struct upump *upump)
{
    return container_of(upump, struct upump_ev, upump);
}

#define UPUMP_EV_DISPATCH_TEMPLATE(ttype)                                   \
/** @This dispatches an event to a watcher for type ##ttype.                \
 *                                                                          \
 * @param ev_loop current event loop (unused parameter)                     \
 * @param ev_#ttype ev watcher                                              \
 * @param revents events triggered (unused parameter)                       \
 */                                                                         \
static void upump_ev_dispatch_##ttype(struct ev_loop *ev_loop,              \
                                      struct ev_##ttype *ev_##ttype,        \
                                      int revents)                          \
{                                                                           \
    struct upump_ev *upump_ev = container_of(ev_##ttype, struct upump_ev,   \
                                             ev_##ttype);                   \
    upump_common_dispatch(&upump_ev->upump);                                \
}

UPUMP_EV_DISPATCH_TEMPLATE(io)
UPUMP_EV_DISPATCH_TEMPLATE(timer)
UPUMP_EV_DISPATCH_TEMPLATE(idle)
#undef UPUMP_EV_DISPATCH_TEMPLATE

/** @This allocates a new upump_ev.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_ev_mgr structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @param event type of event to watch for
 * @param args optional parameters depending on event type
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static struct upump *upump_ev_alloc(struct upump_mgr *mgr, bool source,
                                    enum upump_watcher event, va_list args)
{
    struct upump_ev *upump_ev = malloc(sizeof(struct upump_ev));
    if (unlikely(upump_ev == NULL)) return NULL;

    switch (event) {
#warning expect dereferencing warnings (libev doc says they are bogus)
        case UPUMP_WATCHER_IDLER:
            ev_idle_init(&upump_ev->ev_idle, upump_ev_dispatch_idle);
            break;
        case UPUMP_WATCHER_TIMER: {
            uint64_t after = va_arg(args, uint64_t);
            uint64_t repeat = va_arg(args, uint64_t);
            ev_timer_init(&upump_ev->ev_timer, upump_ev_dispatch_timer,
                          (ev_tstamp)after / UCLOCK_FREQ,
                          (ev_tstamp)repeat / UCLOCK_FREQ);
            break;
        }
        case UPUMP_WATCHER_FD_READ: {
            int fd = va_arg(args, int);
            ev_io_init(&upump_ev->ev_io, upump_ev_dispatch_io, fd,
                       EV_READ);
            break;
        }
        case UPUMP_WATCHER_FD_WRITE: {
            int fd = va_arg(args, int);
            ev_io_init(&upump_ev->ev_io, upump_ev_dispatch_io, fd,
                       EV_WRITE);
            break;
        }
        default:
            free(upump_ev);
            return NULL;
    }

    upump_mgr_use(mgr);

    upump_ev->source = source;
    upump_ev->event = event;

    return upump_ev_to_upump(upump_ev);
}

/** @This starts a watcher, or adds a source watcher to the list.
 *
 * @param upump description structure of the watcher
 * @param start_source when false source watchers will not be started, but
 * added to the list
 * @return false in case of failure
 */
static bool upump_ev_start(struct upump *upump, bool start_source)
{
    struct upump_ev *upump_ev = upump_ev_from_upump(upump);
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(upump->mgr);
    if (unlikely(upump_ev->source && !start_source))
        return upump_common_start_source(upump);

    switch (upump_ev->event) {
        case UPUMP_WATCHER_IDLER:
            ev_idle_start(ev_mgr->ev_loop, &upump_ev->ev_idle);
            break;
        case UPUMP_WATCHER_TIMER:
            ev_timer_start(ev_mgr->ev_loop, &upump_ev->ev_timer);
            break;
        case UPUMP_WATCHER_FD_READ:
        case UPUMP_WATCHER_FD_WRITE:
            ev_io_start(ev_mgr->ev_loop, &upump_ev->ev_io);
            break;
        default:
            return false;
    }
    return true;
}

/** @This stop a watcher, or removes a source watcher from the list.
 *
 * @param upump description structure of the watcher
 * @param stop_source when false source watchers will not be stopped, but
 * removed from the list
 * @return false in case of failure
 */
static bool upump_ev_stop(struct upump *upump, bool stop_source)
{
    struct upump_ev *upump_ev = upump_ev_from_upump(upump);
    struct upump_ev_mgr *ev_mgr =
        upump_ev_mgr_from_upump_mgr(upump->mgr);
    if (unlikely(upump_ev->source && !stop_source))
        return upump_common_stop_source(upump);

    switch (upump_ev->event) {
        case UPUMP_WATCHER_IDLER:
            ev_idle_stop(ev_mgr->ev_loop, &upump_ev->ev_idle);
            break;
        case UPUMP_WATCHER_TIMER:
            ev_timer_stop(ev_mgr->ev_loop, &upump_ev->ev_timer);
            break;
        case UPUMP_WATCHER_FD_READ:
        case UPUMP_WATCHER_FD_WRITE:
            ev_io_stop(ev_mgr->ev_loop, &upump_ev->ev_io);
            break;
        default:
            return false;
    }
    return true;
}

/** @This frees the memory space previously used by a watcher.
 * Please note that the watcher must be stopped before.
 *
 * @param upump description structure of the watcher
 * @param stop_source when false source watchers will not be stopped, but
 * removed from the list
 * @return false in case of failure
 */
static void upump_ev_free(struct upump *upump)
{
    struct upump_ev *upump_ev = upump_ev_from_upump(upump);
    upump_mgr_release(upump->mgr);
    free(upump_ev);
}

/** @This increments the reference count of an ev upump manager.
 *
 * @param mgr pointer to upump manager
 */
static void upump_ev_mgr_use(struct upump_mgr *mgr)
{
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(mgr);
    urefcount_use(&ev_mgr->refcount);
}

/** @This decrements the reference count of a upump manager or frees it
 * (note that all watchers have to be stopped before).
 *
 * @param mgr pointer to upump manager
 */
static void upump_ev_mgr_release(struct upump_mgr *mgr)
{
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(mgr);
    if (unlikely(urefcount_release(&ev_mgr->refcount))) {
        upump_common_mgr_clean(mgr);
        urefcount_clean(&ev_mgr->refcount);
        free(ev_mgr);
    }
}

/** @This allocates and initializes a upump_ev_mgr structure.
 *
 * @param ev_loop pointer to an ev loop
 * @return pointer to the wrapped struct upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc(struct ev_loop *ev_loop)
{
    struct upump_ev_mgr *ev_mgr = malloc(sizeof(struct upump_ev_mgr));
    if (unlikely(ev_mgr == NULL)) return NULL;

    ev_mgr->ev_loop = ev_loop;

    urefcount_init(&ev_mgr->refcount);
    upump_common_mgr_init(&ev_mgr->common_mgr.mgr);

    ev_mgr->common_mgr.mgr.upump_alloc = upump_ev_alloc;
    ev_mgr->common_mgr.mgr.upump_start = upump_ev_start;
    ev_mgr->common_mgr.mgr.upump_stop = upump_ev_stop;
    ev_mgr->common_mgr.mgr.upump_free = upump_ev_free;
    ev_mgr->common_mgr.mgr.upump_mgr_use = upump_ev_mgr_use;
    ev_mgr->common_mgr.mgr.upump_mgr_release = upump_ev_mgr_release;
    return upump_ev_mgr_to_upump_mgr(ev_mgr);
}

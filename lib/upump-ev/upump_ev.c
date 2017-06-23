/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
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
#include <upipe/umutex.h>
#include <upipe/upump.h>
#include <upipe/upump_common.h>
#include <upump-ev/upump_ev.h>

#include <stdlib.h>

#include <ev.h>

/** @This stores management parameters and local structures.
 */
struct upump_ev_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ev private structure */
    struct ev_loop *ev_loop;
    /** true if the loop has to be destroyed at the end */
    bool destroy;

    /** common structure */
    struct upump_common_mgr common_mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(upump_ev_mgr, upump_mgr, upump_mgr, common_mgr.mgr)
UBASE_FROM_TO(upump_ev_mgr, urefcount, urefcount, urefcount)

/** @This stores local structures.
 */
struct upump_ev {
    /** type of event to watch */
    int event;

    /** ev private structure */
    union {
        struct ev_io ev_io;
        struct ev_timer ev_timer;
        struct ev_idle ev_idle;
        struct ev_signal ev_signal;
    };

    /** common structure */
    struct upump_common common;
};

UBASE_FROM_TO(upump_ev, upump, upump, common.upump)

/** @This dispatches an event to a pump for type ev_io.
 *
 * @param ev_loop current event loop (unused parameter)
 * @param ev_io ev pump
 * @param revents events triggered (unused parameter)
 */
static void upump_ev_dispatch_io(struct ev_loop *ev_loop,
                                 struct ev_io *ev_io, int revents)
{
    struct upump_ev *upump_ev = container_of(ev_io, struct upump_ev, ev_io);
    struct upump *upump = upump_ev_to_upump(upump_ev);
    upump_common_dispatch(upump);
}

/** @This dispatches an event to a pump for type ev_timer.
 *
 * @param ev_loop current event loop (unused parameter)
 * @param ev_timer ev pump
 * @param revents events triggered (unused parameter)
 */
static void upump_ev_dispatch_timer(struct ev_loop *ev_loop,
                                    struct ev_timer *ev_timer, int revents)
{
    struct upump_ev *upump_ev = container_of(ev_timer, struct upump_ev,
                                             ev_timer);
    struct upump *upump = upump_ev_to_upump(upump_ev);
    upump_common_dispatch(upump);
}

/** @This dispatches an event to a pump for type ev_idle.
 *
 * @param ev_loop current event loop (unused parameter)
 * @param ev_idle ev pump
 * @param revents events triggered (unused parameter)
 */
static void upump_ev_dispatch_idle(struct ev_loop *ev_loop,
                                   struct ev_idle *ev_idle, int revents)
{
    struct upump_ev *upump_ev = container_of(ev_idle, struct upump_ev, ev_idle);
    struct upump *upump = upump_ev_to_upump(upump_ev);
    upump_common_dispatch(upump);
}

/** @This dispatches an event to a pump for type ev_signal.
 *
 * @param ev_loop current event loop (unused parameter)
 * @param ev_signal ev pump
 * @param revents events triggered (unused parameter)
 */
static void upump_ev_dispatch_signal(struct ev_loop *ev_loop,
                                     struct ev_signal *ev_signal, int revents)
{
    struct upump_ev *upump_ev = container_of(ev_signal, struct upump_ev,
                                             ev_signal);
    struct upump *upump = upump_ev_to_upump(upump_ev);
    upump_common_dispatch(upump);
}

/** @This allocates a new upump_ev.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_ev_mgr structure
 * @param event type of event to watch for
 * @param args optional parameters depending on event type
 * @return pointer to allocated pump, or NULL in case of failure
 */
static struct upump *upump_ev_alloc(struct upump_mgr *mgr,
                                    int event, va_list args)
{
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(mgr);
    struct upump_ev *upump_ev = upool_alloc(&ev_mgr->common_mgr.upump_pool,
                                            struct upump_ev *);
    if (unlikely(upump_ev == NULL))
        return NULL;
    struct upump *upump = upump_ev_to_upump(upump_ev);

    switch (event) {
        case UPUMP_TYPE_IDLER:
            ev_idle_init(&upump_ev->ev_idle, upump_ev_dispatch_idle);
            break;
        case UPUMP_TYPE_TIMER: {
            uint64_t after = va_arg(args, uint64_t);
            uint64_t repeat = va_arg(args, uint64_t);
            ev_timer_init(&upump_ev->ev_timer, upump_ev_dispatch_timer,
                          (ev_tstamp)after / UCLOCK_FREQ,
                          (ev_tstamp)repeat / UCLOCK_FREQ);
            break;
        }
        case UPUMP_TYPE_FD_READ: {
            int fd = va_arg(args, int);
            ev_io_init(&upump_ev->ev_io, upump_ev_dispatch_io, fd, EV_READ);
            break;
        }
        case UPUMP_TYPE_FD_WRITE: {
            int fd = va_arg(args, int);
            ev_io_init(&upump_ev->ev_io, upump_ev_dispatch_io, fd, EV_WRITE);
            break;
        }
        case UPUMP_TYPE_SIGNAL: {
            int signal = va_arg(args, int);
            ev_signal_init(&upump_ev->ev_signal, upump_ev_dispatch_signal,
                           signal);
            break;
        }
        default:
            free(upump_ev);
            return NULL;
    }
    upump_ev->event = event;

    upump_common_init(upump);

    return upump;
}

/** @This starts a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_ev_real_start(struct upump *upump, bool status)
{
    struct upump_ev *upump_ev = upump_ev_from_upump(upump);
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(upump->mgr);

    switch (upump_ev->event) {
        case UPUMP_TYPE_IDLER:
            ev_idle_start(ev_mgr->ev_loop, &upump_ev->ev_idle);
            break;
        case UPUMP_TYPE_TIMER:
            ev_timer_start(ev_mgr->ev_loop, &upump_ev->ev_timer);
            break;
        case UPUMP_TYPE_FD_READ:
        case UPUMP_TYPE_FD_WRITE:
            ev_io_start(ev_mgr->ev_loop, &upump_ev->ev_io);
            break;
        case UPUMP_TYPE_SIGNAL:
            ev_signal_start(ev_mgr->ev_loop, &upump_ev->ev_signal);
            break;
        default:
            break;
    }
    if (!status)
        ev_unref(ev_mgr->ev_loop);
}

/** @This stops a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_ev_real_stop(struct upump *upump, bool status)
{
    struct upump_ev *upump_ev = upump_ev_from_upump(upump);
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(upump->mgr);

    if (!status)
        ev_ref(ev_mgr->ev_loop);
    switch (upump_ev->event) {
        case UPUMP_TYPE_IDLER:
            ev_idle_stop(ev_mgr->ev_loop, &upump_ev->ev_idle);
            break;
        case UPUMP_TYPE_TIMER:
            ev_timer_stop(ev_mgr->ev_loop, &upump_ev->ev_timer);
            break;
        case UPUMP_TYPE_FD_READ:
        case UPUMP_TYPE_FD_WRITE:
            ev_io_stop(ev_mgr->ev_loop, &upump_ev->ev_io);
            break;
        case UPUMP_TYPE_SIGNAL:
            ev_signal_stop(ev_mgr->ev_loop, &upump_ev->ev_signal);
            break;
        default:
            break;
    }
}

/** @This released the memory space previously used by a pump.
 * Please note that the pump must be stopped before.
 *
 * @param upump description structure of the pump
 */
static void upump_ev_free(struct upump *upump)
{
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(upump->mgr);
    upump_stop(upump);
    upump_common_clean(upump);
    struct upump_ev *upump_ev = upump_ev_from_upump(upump);
    upool_free(&ev_mgr->common_mgr.upump_pool, upump_ev);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to upump_ev or NULL in case of allocation error
 */
static void *upump_ev_alloc_inner(struct upool *upool)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_pool(upool);
    struct upump_ev *upump_ev = malloc(sizeof(struct upump_ev));
    if (unlikely(upump_ev == NULL))
        return NULL;
    struct upump *upump = upump_ev_to_upump(upump_ev);
    upump->mgr = upump_common_mgr_to_upump_mgr(common_mgr);
    return upump_ev;
}

/** @internal @This frees a upump_ev.
 *
 * @param upool pointer to upool
 * @param upump_ev pointer to a upump_ev structure to free
 */
static void upump_ev_free_inner(struct upool *upool, void *upump_ev)
{
    free(upump_ev);
}

/** @This processes control commands on a upump_ev.
 *
 * @param upump description structure of the pump
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upump_ev_control(struct upump *upump, int command, va_list args)
{
    switch (command) {
        case UPUMP_START:
            upump_common_start(upump);
            return UBASE_ERR_NONE;
        case UPUMP_STOP:
            upump_common_stop(upump);
            return UBASE_ERR_NONE;
        case UPUMP_FREE:
            upump_ev_free(upump);
            return UBASE_ERR_NONE;
        case UPUMP_GET_STATUS: {
            int *status_p = va_arg(args, int *);
            upump_common_get_status(upump, status_p);
            return UBASE_ERR_NONE;
        }
        case UPUMP_SET_STATUS: {
            int status = va_arg(args, int);
            upump_common_set_status(upump, status);
            return UBASE_ERR_NONE;
        }
        case UPUMP_ALLOC_BLOCKER: {
            struct upump_blocker **p = va_arg(args, struct upump_blocker **);
            *p = upump_common_blocker_alloc(upump);
            return UBASE_ERR_NONE;
        }
        case UPUMP_FREE_BLOCKER: {
            struct upump_blocker *blocker =
                va_arg(args, struct upump_blocker *);
            upump_common_blocker_free(blocker);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This is called when the event loop starts invoking watchers.
 *
 * @param ev_loop ev loop
 **/
static void upump_ev_mgr_lock(struct ev_loop *loop)
{
    struct umutex *mutex = ev_userdata(loop);
    umutex_lock(mutex);
}

/** @internal @This is called when the event loop goes to sleep.
 *
 * @param ev_loop ev loop
 **/
static void upump_ev_mgr_unlock(struct ev_loop *loop)
{
    struct umutex *mutex = ev_userdata(loop);
    umutex_unlock(mutex);
}

/** @internal @This runs an event loop.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param mutex mutual exclusion primitives to access the event loop
 * @return an error code, including @ref UBASE_ERR_BUSY if a pump is still
 * active
 */
static int upump_ev_mgr_run(struct upump_mgr *mgr, struct umutex *mutex)
{
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(mgr);

    if (mutex != NULL) {
        ev_set_userdata(ev_mgr->ev_loop, mutex);
        ev_set_loop_release_cb(ev_mgr->ev_loop,
                               upump_ev_mgr_unlock, upump_ev_mgr_lock);

        upump_ev_mgr_lock(ev_mgr->ev_loop);
    }

    bool status;
#if EV_VERSION_MAJOR > 4 || (EV_VERSION_MAJOR == 4 && EV_VERSION_MINOR > 11)
	status = ev_run(ev_mgr->ev_loop, 0);
#else
	status = false;
	ev_run(ev_mgr->ev_loop, 0);
#endif


    if (mutex != NULL)
        upump_ev_mgr_unlock(ev_mgr->ev_loop);

    return status ? UBASE_ERR_BUSY : UBASE_ERR_NONE;
}

/** @This processes control commands on a upump_ev_mgr.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upump_ev_mgr_control(struct upump_mgr *mgr,
                                int command, va_list args)
{
    switch (command) {
        case UPUMP_MGR_RUN: {
            struct umutex *mutex = va_arg(args, struct umutex *);
            return upump_ev_mgr_run(mgr, mutex);
        }
        case UPUMP_MGR_VACUUM:
            upump_common_mgr_vacuum(mgr);
            return UBASE_ERR_NONE;
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upump manager.
 *
 * @param urefcount pointer to urefcount
 */
static void upump_ev_mgr_free(struct urefcount *urefcount)
{
    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_urefcount(urefcount);
    upump_common_mgr_clean(upump_ev_mgr_to_upump_mgr(ev_mgr));
    if (ev_mgr->destroy)
        ev_loop_destroy(ev_mgr->ev_loop);
    free(ev_mgr);
}

/** @This allocates and initializes a upump_ev_mgr structure.
 *
 * @param ev_loop pointer to an ev loop
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc(struct ev_loop *ev_loop,
                                     uint16_t upump_pool_depth,
                                     uint16_t upump_blocker_pool_depth)
{
    struct upump_ev_mgr *ev_mgr =
        malloc(sizeof(struct upump_ev_mgr) +
               upump_common_mgr_sizeof(upump_pool_depth,
                                       upump_blocker_pool_depth));
    if (unlikely(ev_mgr == NULL))
        return NULL;

    struct upump_mgr *mgr = upump_ev_mgr_to_upump_mgr(ev_mgr);
    mgr->signature = UPUMP_EV_SIGNATURE;
    urefcount_init(upump_ev_mgr_to_urefcount(ev_mgr), upump_ev_mgr_free);
    ev_mgr->common_mgr.mgr.refcount = upump_ev_mgr_to_urefcount(ev_mgr);
    ev_mgr->common_mgr.mgr.upump_alloc = upump_ev_alloc;
    ev_mgr->common_mgr.mgr.upump_control = upump_ev_control;
    ev_mgr->common_mgr.mgr.upump_mgr_control = upump_ev_mgr_control;

    upump_common_mgr_init(mgr, upump_pool_depth, upump_blocker_pool_depth,
                          ev_mgr->upool_extra,
                          upump_ev_real_start, upump_ev_real_stop,
                          upump_ev_alloc_inner, upump_ev_free_inner);

    ev_mgr->ev_loop = ev_loop;
    ev_mgr->destroy = false;
    return mgr;
}

/** @This allocates and initializes a upump_mgr structure bound to the
 * default ev loop.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc_default(uint16_t upump_pool_depth,
                                             uint16_t upump_blocker_pool_depth)
{
    struct ev_loop *loop = ev_default_loop(0);
    if (loop == NULL)
        return NULL;

    struct upump_mgr *mgr = upump_ev_mgr_alloc(loop, upump_pool_depth,
                                               upump_blocker_pool_depth);
    if (mgr == NULL)
        return NULL;

    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(mgr);
    ev_mgr->destroy = true;

    return mgr;
}

/** @This allocates and initializes a upump_mgr structure bound to an
 * allocated ev loop.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc_loop(uint16_t upump_pool_depth,
                                          uint16_t upump_blocker_pool_depth)
{
    struct ev_loop *loop = ev_loop_new(0);
    if (loop == NULL)
        return NULL;

    struct upump_mgr *mgr = upump_ev_mgr_alloc(loop, upump_pool_depth,
                                               upump_blocker_pool_depth);
    if (mgr == NULL)
        return NULL;

    struct upump_ev_mgr *ev_mgr = upump_ev_mgr_from_upump_mgr(mgr);
    ev_mgr->destroy = true;

    return mgr;
}

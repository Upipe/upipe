/*
 * Copyright (C) 2013
 *
 * Authors: Cedric Bail
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
 * @short implementation of a Upipe event loop using Ecore
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/upump.h>
#include <upipe/upump_common.h>
#include <upump-ecore/upump_ecore.h>

#include <stdlib.h>

#include <Ecore.h>

/** @This stores management parameters and local structures.
 */
struct upump_ecore_mgr {
    /** common structure */
    struct upump_common_mgr common_mgr;
};

/** @This stores local structures.
 */
struct upump_ecore {
    /** common structure */
    struct upump upump;
    /** ev private structure */
    union {
        Ecore_Fd_Handler *io;
        Ecore_Timer *timer;
        Ecore_Idler *idle;
    };
    uint64_t repeat;

    /** type of event to watch */
    enum upump_watcher event;
    /** true for a source watcher */
    bool source;
};

/** @internal @This returns the high-level upump_mgr structure.
 *
 * @param ev_mgr pointer to the upump_ecore_mgr structure
 * @return pointer to the upump_mgr structure
 */
static inline struct upump_mgr *upump_ecore_mgr_to_upump_mgr(struct upump_ecore_mgr *ev_mgr)
{
    return upump_common_mgr_to_upump_mgr(&ev_mgr->common_mgr);
}

/** @internal @This returns the private upump_ecore_mgr structure.
 *
 * @param mgr pointer to the upump_mgr structure
 * @return pointer to the upump_ecore_mgr structure
 */
static inline struct upump_ecore_mgr *upump_ecore_mgr_from_upump_mgr(struct upump_mgr *mgr)
{
    struct upump_common_mgr *common_mgr = upump_common_mgr_from_upump_mgr(mgr);
    return container_of(common_mgr, struct upump_ecore_mgr, common_mgr);
}

/** @internal @This returns the high-level upump structure.
 *
 * @param ev pointer to the upump_ecore structure
 * @return pointer to the upump structure
 */
static inline struct upump *upump_ecore_to_upump(struct upump_ecore *upump_ecore)
{
    return &upump_ecore->upump;
}

/** @internal @This returns the private upump_ecore structure.
 *
 * @param mgr pointer to the upump structure
 * @return pointer to the upump_ecore structure
 */
static inline struct upump_ecore *upump_ecore_from_upump(struct upump *upump)
{
    return container_of(upump, struct upump_ecore, upump);
}

static Eina_Bool
_ecore_upump_idler_dispatch(void *data)
{
    struct upump_ecore *upump_ecore = data;

    upump_common_dispatch(&upump_ecore->upump);
    return EINA_TRUE;
}

static Eina_Bool
_ecore_upump_timer_dispatch(void *data)
{
    struct upump_ecore *upump_ecore = data;

    if (upump_ecore->repeat > 0) {
        ecore_timer_interval_set(upump_ecore->timer, (double) upump_ecore->repeat / UCLOCK_FREQ);
	upump_ecore->repeat = 0;
    }

    upump_common_dispatch(&upump_ecore->upump);
    return EINA_TRUE;
}

static Eina_Bool
_ecore_upump_fd_dispatch(void *data, Ecore_Fd_Handler *fd_handler)
{
    struct upump_ecore *upump_ecore = data;

    upump_common_dispatch(&upump_ecore->upump);
    return EINA_TRUE;
}

/** @This allocates a new upump_ecore.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_ecore_mgr structure
 * @param source true if this watcher is a source and should block when sinks
 * are blocked
 * @param event type of event to watch for
 * @param args optional parameters depending on event type
 * @return pointer to allocated watcher, or NULL in case of failure
 */
static struct upump *upump_ecore_alloc(struct upump_mgr *mgr, bool source,
				       enum upump_watcher event, va_list args)
{
    struct upump_ecore *upump_ecore = malloc(sizeof(struct upump_ecore));
    if (unlikely(upump_ecore == NULL)) return NULL;

    switch (event) {
        case UPUMP_WATCHER_IDLER:
            break;
        case UPUMP_WATCHER_TIMER: {
            uint64_t after = va_arg(args, uint64_t);
            uint64_t repeat = va_arg(args, uint64_t);
	    upump_ecore->timer = ecore_timer_add((double) after / UCLOCK_FREQ, _ecore_upump_timer_dispatch, upump_ecore);
	    ecore_timer_freeze(upump_ecore->timer);
	    upump_ecore->repeat = repeat;
            break;
        }
        case UPUMP_WATCHER_FD_READ:
        case UPUMP_WATCHER_FD_WRITE: {
            int fd = va_arg(args, int);
	    upump_ecore->io = ecore_main_fd_handler_add(fd, 0, _ecore_upump_fd_dispatch, upump_ecore, NULL, NULL);
            break;
        }
        default:
            free(upump_ecore);
            return NULL;
    }

    upump_mgr_use(mgr);

    upump_ecore->source = source;
    upump_ecore->event = event;

    return upump_ecore_to_upump(upump_ecore);
}

/** @This starts a watcher, or adds a source watcher to the list.
 *
 * @param upump description structure of the watcher
 * @param start_source when false source watchers will not be started, but
 * added to the list
 * @return false in case of failure
 */
static bool upump_ecore_start(struct upump *upump, bool start_source)
{
    struct upump_ecore *upump_ecore = upump_ecore_from_upump(upump);
    if (unlikely(upump_ecore->source && !start_source))
        return upump_common_start_source(upump);

    switch (upump_ecore->event) {
        case UPUMP_WATCHER_IDLER:
	    upump_ecore->idle = ecore_idler_add(_ecore_upump_idler_dispatch, upump_ecore);
            break;
        case UPUMP_WATCHER_TIMER:
	    ecore_timer_thaw(upump_ecore->timer);
            break;
        case UPUMP_WATCHER_FD_READ:
	    ecore_main_fd_handler_active_set(upump_ecore->io, ECORE_FD_READ);
	    break;
        case UPUMP_WATCHER_FD_WRITE:
	    ecore_main_fd_handler_active_set(upump_ecore->io, ECORE_FD_WRITE);
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
static bool upump_ecore_stop(struct upump *upump, bool stop_source)
{
    struct upump_ecore *upump_ecore = upump_ecore_from_upump(upump);
    if (unlikely(upump_ecore->source && !stop_source))
        return upump_common_stop_source(upump);

    switch (upump_ecore->event) {
        case UPUMP_WATCHER_IDLER:
	    ecore_idler_del(upump_ecore->idle);
	    upump_ecore->idle = NULL;
            break;
        case UPUMP_WATCHER_TIMER:
	    ecore_timer_freeze(upump_ecore->timer);
            break;
        case UPUMP_WATCHER_FD_READ:
        case UPUMP_WATCHER_FD_WRITE:
	    ecore_main_fd_handler_active_set(upump_ecore->io, 0);
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
static void upump_ecore_free(struct upump *upump)
{
    struct upump_ecore *upump_ecore = upump_ecore_from_upump(upump);
    switch (upump_ecore->event) {
        case UPUMP_WATCHER_IDLER:
	    if (upump_ecore->idle) ecore_idler_del(upump_ecore->idle);
	    upump_ecore->idle = NULL;
            break;
        case UPUMP_WATCHER_TIMER:
	    ecore_timer_del(upump_ecore->timer);
            break;
        case UPUMP_WATCHER_FD_READ:
        case UPUMP_WATCHER_FD_WRITE:
	    ecore_main_fd_handler_del(upump_ecore->io);
            break;
    }
    upump_mgr_release(upump->mgr);
    free(upump_ecore);
}

/** @This frees a upump manager.
 *
 * @param mgr pointer to upump manager
 */
static void upump_ecore_mgr_free(struct upump_mgr *mgr)
{
    struct upump_ecore_mgr *ev_mgr = upump_ecore_mgr_from_upump_mgr(mgr);
    upump_common_mgr_clean(mgr);
    free(ev_mgr);
}

/** @This allocates and initializes a upump_ecore_mgr structure.
 *
 * @param ev_loop pointer to an ev loop
 * @return pointer to the wrapped struct upump_mgr structure
 */
struct upump_mgr *upump_ecore_mgr_alloc(void)
{
    struct upump_ecore_mgr *ev_mgr = malloc(sizeof(struct upump_ecore_mgr));
    if (unlikely(ev_mgr == NULL)) return NULL;

    upump_common_mgr_init(&ev_mgr->common_mgr.mgr);

    ev_mgr->common_mgr.mgr.upump_alloc = upump_ecore_alloc;
    ev_mgr->common_mgr.mgr.upump_start = upump_ecore_start;
    ev_mgr->common_mgr.mgr.upump_stop = upump_ecore_stop;
    ev_mgr->common_mgr.mgr.upump_free = upump_ecore_free;
    ev_mgr->common_mgr.mgr.upump_mgr_free = upump_ecore_mgr_free;
    return upump_ecore_mgr_to_upump_mgr(ev_mgr);
}

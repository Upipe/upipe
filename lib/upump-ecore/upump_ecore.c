/*
 * Copyright (C) 2014-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Cedric Bail <cedric.bail@free.fr>
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

#include <upipe/urefcount_helper.h>
#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/umutex.h>
#include <upipe/upump.h>
#include <upipe/upump_common.h>
#include <upump-ecore/upump_ecore.h>

#include <stdlib.h>

#include <Ecore.h>

/** @This stores management parameters and local structures.
 */
struct upump_ecore_mgr {
    /** refcount management structure */
    struct urefcount urefcount;
    /** event refcount management structure */
    struct urefcount urefcount_event;

    /** common structure */
    struct upump_common_mgr common_mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(upump_ecore_mgr, upump_mgr, upump_mgr, common_mgr.mgr)
UBASE_FROM_TO(upump_ecore_mgr, urefcount, urefcount, urefcount)
UREFCOUNT_HELPER(upump_ecore_mgr, urefcount_event, upump_ecore_mgr_no_event);

/** @This stores local structures.
 */
struct upump_ecore {
    /** type of event to watch */
    int event;

    /** Ecore private structure */
    union {
        Ecore_Fd_Handler *io;
        Ecore_Timer *timer;
        Ecore_Idler *idle;
    };
    uint64_t repeat;
    bool repeated;

    /** common structure */
    struct upump_common common;
    /** pointer to manager event refcount is needed */
    struct urefcount *urefcount_event;
};

UBASE_FROM_TO(upump_ecore, upump, upump, common.upump)

/** @This dispatches an event to a pump for type Ecore_Fd_Handler.
 *
 * @param _upump_ecore upump_ecore private structure pointer
 * @param fd_handler Ecore fd handler
 * @return EINA_TRUE
 */
static Eina_Bool upump_ecore_dispatch_fd(void *_upump_ecore, Ecore_Fd_Handler *fd_handler)
{
    struct upump_ecore *upump_ecore = _upump_ecore;
    struct upump *upump = upump_ecore_to_upump(upump_ecore);
    struct upump_ecore_mgr *ecore_mgr =
        upump_ecore_mgr_from_upump_mgr(upump->mgr);
    upump_common_dispatch(upump);
    if (upump_ecore_mgr_single_urefcount_event(ecore_mgr))
        ecore_main_loop_quit();
    return EINA_TRUE;
}

/** @This dispatches an event to a pump for type Ecore_Timer.
 *
 * @param _upump_ecore upump_ecore private structure pointer
 * @return EINA_TRUE
 */
static Eina_Bool upump_ecore_dispatch_timer(void *_upump_ecore)
{
    struct upump_ecore *upump_ecore = _upump_ecore;
    struct upump *upump = upump_ecore_to_upump(upump_ecore);
    struct upump_ecore_mgr *ecore_mgr =
        upump_ecore_mgr_from_upump_mgr(upump->mgr);

    if (unlikely(!upump_ecore->repeat)) {
        ecore_timer_freeze(upump_ecore->timer);
        urefcount_release(upump_ecore->urefcount_event);
        upump_ecore->urefcount_event = NULL;
    } else if (unlikely(!upump_ecore->repeated)) {
        ecore_timer_interval_set(upump_ecore->timer,
                    (double) upump_ecore->repeat / UCLOCK_FREQ);
        upump_ecore->repeated = true;
    }

    upump_common_dispatch(upump);
    if (upump_ecore_mgr_single_urefcount_event(ecore_mgr))
        ecore_main_loop_quit();

    return EINA_TRUE;
}

/** @This dispatches an event to a pump for type Ecore_Idler.
 *
 * @param _upump_ecore upump_ecore private structure pointer
 * @return EINA_TRUE
 */
static Eina_Bool upump_ecore_dispatch_idle(void *_upump_ecore)
{
    struct upump_ecore *upump_ecore = _upump_ecore;
    struct upump *upump = upump_ecore_to_upump(upump_ecore);
    struct upump_ecore_mgr *ecore_mgr =
        upump_ecore_mgr_from_upump_mgr(upump->mgr);
    upump_common_dispatch(upump);
    if (upump_ecore_mgr_single_urefcount_event(ecore_mgr))
        ecore_main_loop_quit();

    return EINA_TRUE;
}

/** @This allocates a new upump_ecore.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_ecore_mgr structure
 * @param event type of event to watch for
 * @param args optional parameters depending on event type
 * @return pointer to allocated pump, or NULL in case of failure
 */
static struct upump *upump_ecore_alloc(struct upump_mgr *mgr,
                                       int event, va_list args)
{
    struct upump_ecore_mgr *ecore_mgr = upump_ecore_mgr_from_upump_mgr(mgr);
    struct upump_ecore *upump_ecore = upool_alloc(&ecore_mgr->common_mgr.upump_pool,
                                            struct upump_ecore *);
    if (unlikely(upump_ecore == NULL))
        return NULL;
    struct upump *upump = upump_ecore_to_upump(upump_ecore);

    switch (event) {
        case UPUMP_TYPE_IDLER:
            break;
        case UPUMP_TYPE_TIMER: {
            uint64_t after = va_arg(args, uint64_t);
            uint64_t repeat = va_arg(args, uint64_t);

            upump_ecore->timer = ecore_timer_add((double) after / UCLOCK_FREQ,
                                     upump_ecore_dispatch_timer, upump_ecore);
            assert(upump_ecore->timer);
            ecore_timer_freeze(upump_ecore->timer);
            upump_ecore->repeat = repeat;
            upump_ecore->repeated = false;
            break;
        }
        case UPUMP_TYPE_FD_READ: {
            int fd = va_arg(args, int);
            upump_ecore->io = ecore_main_fd_handler_add(fd, ECORE_FD_READ,
                        upump_ecore_dispatch_fd, upump_ecore, NULL, NULL);
            ecore_main_fd_handler_active_set(upump_ecore->io, 0);
            assert(upump_ecore->io);
            break;
        }
        case UPUMP_TYPE_FD_WRITE: {
            int fd = va_arg(args, int);
            upump_ecore->io = ecore_main_fd_handler_add(fd, ECORE_FD_WRITE,
                        upump_ecore_dispatch_fd, upump_ecore, NULL, NULL);
            ecore_main_fd_handler_active_set(upump_ecore->io, 0);
            assert(upump_ecore->io);
            break;
        }
        default:
            free(upump_ecore);
            return NULL;
    }
    upump_ecore->event = event;
    upump_ecore->urefcount_event = NULL;

    upump_mgr_use(mgr);
    upump_common_init(upump);

    return upump;
}

/** @This starts a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_ecore_real_start(struct upump *upump, bool status)
{
    struct upump_ecore *upump_ecore = upump_ecore_from_upump(upump);
    struct upump_ecore_mgr *ecore_mgr =
        upump_ecore_mgr_from_upump_mgr(upump->mgr);

    switch (upump_ecore->event) {
        case UPUMP_TYPE_IDLER:
            upump_ecore->idle = ecore_idler_add(upump_ecore_dispatch_idle,
                                                upump_ecore);
            break;
        case UPUMP_TYPE_TIMER:
            ecore_timer_thaw(upump_ecore->timer);
            break;
        case UPUMP_TYPE_FD_READ:
            ecore_main_fd_handler_active_set(upump_ecore->io, ECORE_FD_READ);
            break;
        case UPUMP_TYPE_FD_WRITE:
            ecore_main_fd_handler_active_set(upump_ecore->io, ECORE_FD_WRITE);
            break;
        default:
            break;
    }

    if (status) {
        assert(!upump_ecore->urefcount_event);
        upump_ecore_mgr_use_urefcount_event(ecore_mgr);
        upump_ecore->urefcount_event =
            &ecore_mgr->urefcount_event;
    }
}

/** @This stops a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_ecore_real_stop(struct upump *upump, bool status)
{
    struct upump_ecore *upump_ecore = upump_ecore_from_upump(upump);
    struct upump_ecore_mgr *ecore_mgr =
        upump_ecore_mgr_from_upump_mgr(upump->mgr);

    switch (upump_ecore->event) {
        case UPUMP_TYPE_IDLER:
            if (upump_ecore->idle) {
                ecore_idler_del(upump_ecore->idle);
            }
            upump_ecore->idle = NULL;
            break;
        case UPUMP_TYPE_TIMER:
            ecore_timer_freeze(upump_ecore->timer);
            break;
        case UPUMP_TYPE_FD_READ:
        case UPUMP_TYPE_FD_WRITE:
            ecore_main_fd_handler_active_set(upump_ecore->io, 0);
            break;
        default:
            break;
    }

    if (status) {
        urefcount_release(upump_ecore->urefcount_event);
        upump_ecore->urefcount_event = NULL;
    }
}

/** @This released the memory space previously used by a pump.
 * Please note that the pump must be stopped before.
 *
 * @param upump description structure of the pump
 */
static void upump_ecore_free(struct upump *upump)
{
    struct upump_ecore_mgr *ecore_mgr = upump_ecore_mgr_from_upump_mgr(upump->mgr);
    upump_stop(upump);
    upump_common_clean(upump);
    struct upump_ecore *upump_ecore = upump_ecore_from_upump(upump);
    upool_free(&ecore_mgr->common_mgr.upump_pool, upump_ecore);
    upump_mgr_release(&ecore_mgr->common_mgr.mgr);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to upump_ecore or NULL in case of allocation error
 */
static void *upump_ecore_alloc_inner(struct upool *upool)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_pool(upool);
    struct upump_ecore *upump_ecore = malloc(sizeof(struct upump_ecore));
    if (unlikely(upump_ecore == NULL))
        return NULL;
    struct upump *upump = upump_ecore_to_upump(upump_ecore);
    upump->mgr = upump_common_mgr_to_upump_mgr(common_mgr);
    return upump_ecore;
}

/** @internal @This frees a upump_ecore.
 *
 * @param upool pointer to upool
 * @param upump_ecore pointer to a upump_ecore structure to free
 */
static void upump_ecore_free_inner(struct upool *upool, void *upump_ecore)
{
    free(upump_ecore);
}

/** @This processes control commands on a upump_ecore.
 *
 * @param upump description structure of the pump
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upump_ecore_control(struct upump *upump, int command, va_list args)
{
    switch (command) {
        case UPUMP_START:
            upump_common_start(upump);
            return UBASE_ERR_NONE;
        case UPUMP_STOP:
            upump_common_stop(upump);
            return UBASE_ERR_NONE;
        case UPUMP_FREE:
            upump_ecore_free(upump);
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

/** @internal @This is called when there is no more event in the loop.
 *
 * @param upump_ecore_mgr ecore management structure
 */
static void upump_ecore_mgr_no_event(struct upump_ecore_mgr *ecore_mgr)
{
}

/** @internal @This runs an event loop.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param mutex mutual exclusion primitives to access the event loop
 * @return an error code, including @ref UBASE_ERR_BUSY if a pump is still
 * active
 */
static int upump_ecore_mgr_run(struct upump_mgr *mgr, struct umutex *mutex)
{
    struct upump_ecore_mgr *ecore_mgr = upump_ecore_mgr_from_upump_mgr(mgr);
    if (mutex != NULL)
        return UBASE_ERR_UNHANDLED;

    if (upump_ecore_mgr_single_urefcount_event(ecore_mgr))
        return UBASE_ERR_NONE;
    ecore_main_loop_begin();

    return UBASE_ERR_NONE;
}

/** @This processes control commands on a upump_ecore_mgr.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upump_ecore_mgr_control(struct upump_mgr *mgr,
                                   int command, va_list args)
{
    switch (command) {
        case UPUMP_MGR_RUN: {
            struct umutex *mutex = va_arg(args, struct umutex *);
            return upump_ecore_mgr_run(mgr, mutex);
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
static void upump_ecore_mgr_free(struct urefcount *urefcount)
{
    struct upump_ecore_mgr *ecore_mgr = upump_ecore_mgr_from_urefcount(urefcount);
    upump_ecore_mgr_clean_urefcount_event(ecore_mgr);
    upump_common_mgr_clean(upump_ecore_mgr_to_upump_mgr(ecore_mgr));
    free(ecore_mgr);
    ecore_shutdown();
}

/** @This allocates and initializes a upump_ecore_mgr structure.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ecore_mgr_alloc(uint16_t upump_pool_depth,
                                        uint16_t upump_blocker_pool_depth)
{
    if (!ecore_init())
        return NULL;

    struct upump_ecore_mgr *ecore_mgr =
        malloc(sizeof(struct upump_ecore_mgr) +
               upump_common_mgr_sizeof(upump_pool_depth,
                                       upump_blocker_pool_depth));
    if (unlikely(ecore_mgr == NULL))
        return NULL;

    struct upump_mgr *mgr = upump_ecore_mgr_to_upump_mgr(ecore_mgr);
    mgr->signature = UPUMP_ECORE_SIGNATURE;
    urefcount_init(upump_ecore_mgr_to_urefcount(ecore_mgr), upump_ecore_mgr_free);
    upump_ecore_mgr_init_urefcount_event(ecore_mgr);
    ecore_mgr->common_mgr.mgr.refcount = upump_ecore_mgr_to_urefcount(ecore_mgr);
    ecore_mgr->common_mgr.mgr.upump_alloc = upump_ecore_alloc;
    ecore_mgr->common_mgr.mgr.upump_control = upump_ecore_control;
    ecore_mgr->common_mgr.mgr.upump_mgr_control = upump_ecore_mgr_control;

    upump_common_mgr_init(mgr, upump_pool_depth, upump_blocker_pool_depth,
                          ecore_mgr->upool_extra,
                          upump_ecore_real_start, upump_ecore_real_stop,
                          upump_ecore_alloc_inner, upump_ecore_free_inner);
    return mgr;
}

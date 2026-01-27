/*
 * Copyright (C) 2021 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short implementation of a Upipe event loop using libsrt
 */

#include "upipe/ubase.h"
#include "upipe/urefcount.h"
#include "upipe/uclock.h"
#include "upipe/upump.h"
#include "upipe/upump_common.h"
#include "upump-srt/upump_srt.h"

#include <stdlib.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>

#include <srt/srt.h>

/** @This stores management parameters and local structures.
 */
struct upump_srt_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** srt epoll id */
    int epoll_id;
    /** number of idler events */
    unsigned idlers;
    /** currently iterating the upumps list */
    bool running;
    /** list of allocated upump structures */
    struct uchain upumps;

    /** common structure */
    struct upump_common_mgr common_mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(upump_srt_mgr, upump_mgr, upump_mgr, common_mgr.mgr)
UBASE_FROM_TO(upump_srt_mgr, urefcount, urefcount, urefcount)

/** @This stores local structures.
 */
struct upump_srt {
    /** structure for double-linked list */
    struct uchain uchain;

    /** type of event to watch */
    int event;

    /** ev private structure */
    union {
        int fd;
        SRTSOCKET socket;
    };

    /** private structure */
    union {
        struct {
            uint64_t after;
            uint64_t repeat;
            bool expired;
        } timer;
        int signal;
    };

    /** upump should be freed after upumps list traversal */
    bool free;

    /** common structure */
    struct upump_common common;
};

UBASE_FROM_TO(upump_srt, upump, upump, common.upump)
UBASE_FROM_TO(upump_srt, uchain, uchain, uchain)

/** @This allocates a new upump_srt.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_srt_mgr structure
 * @param event type of event to watch for
 * @param args optional parameters depending on event type
 * @return pointer to allocated pump, or NULL in case of failure
 */
static struct upump *upump_srt_alloc(struct upump_mgr *mgr,
                                     int event, va_list args)
{
    if (event >= UPUMP_TYPE_LOCAL) {
        unsigned int signature = va_arg(args, unsigned int);
        if (signature != mgr->signature)
            return NULL;
    }

    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(mgr);
    struct upump_srt *upump_srt = upool_alloc(&srt_mgr->common_mgr.upump_pool,
                                              struct upump_srt *);
    if (unlikely(upump_srt == NULL))
        return NULL;
    struct upump *upump = upump_srt_to_upump(upump_srt);

    switch (event) {
        case UPUMP_TYPE_IDLER:
            upump_srt->fd = -1;
            break;
        case UPUMP_TYPE_TIMER: {
            uint64_t after = va_arg(args, uint64_t);
            uint64_t repeat = va_arg(args, uint64_t);
            int fd = timerfd_create(CLOCK_MONOTONIC, 0);
            if (fd == -1) {
                free(upump_srt);
                return NULL;
            }
            if (after == 0)
                after = repeat;
            upump_srt->fd = fd;
            upump_srt->timer.after = after;
            upump_srt->timer.repeat = repeat;
            break;
        }
        case UPUMP_TYPE_FD_READ: {
            int fd = va_arg(args, int);
            upump_srt->fd = fd;
            break;
        }
        case UPUMP_TYPE_FD_WRITE: {
            int fd = va_arg(args, int);
            upump_srt->fd = fd;
            break;
        }
        case UPUMP_TYPE_SIGNAL: {
            int signal = va_arg(args, int);
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, signal);
            int fd = signalfd(-1, &mask, 0);
            if (fd == -1) {
                free(upump_srt);
                return NULL;
            }
            upump_srt->fd = fd;
            upump_srt->signal = signal;
            break;
        }
        case UPUMP_SRT_TYPE_READ: {
            SRTSOCKET socket = va_arg(args, int);
            upump_srt->socket = socket;
            break;
        }
        case UPUMP_SRT_TYPE_WRITE: {
            SRTSOCKET socket = va_arg(args, int);
            upump_srt->socket = socket;
            break;
        }
        default:
            free(upump_srt);
            return NULL;
    }
    uchain_init(&upump_srt->uchain);
    upump_srt->event = event;
    upump_srt->free = false;
    ulist_add(&srt_mgr->upumps, &upump_srt->uchain);

    upump_common_init(upump);

    return upump;
}

/** @This lookup a upump_srt structure by fd and event.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param fd file descriptor or srt socket
 * @param event type of event
 * @return pointer to corresponding pump, or NULL if not found
 */
static struct upump_srt *upump_srt_lookup(struct upump_mgr *mgr,
                                          int fd, int event)
{
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(mgr);
    struct uchain *uchain;
    ulist_foreach(&srt_mgr->upumps, uchain) {
        struct upump_srt *upump_srt = upump_srt_from_uchain(uchain);
        if (!upump_srt->common.started)
            continue;
        int upump_srt_event;
        switch (upump_srt->event) {
            case UPUMP_TYPE_TIMER:
            case UPUMP_TYPE_SIGNAL:
                upump_srt_event = UPUMP_TYPE_FD_READ;
                break;
            default:
                upump_srt_event = upump_srt->event;
        }
        if (upump_srt_event == event && upump_srt->fd == fd)
            return upump_srt;
    }
    return NULL;
}

/** @This starts a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_srt_real_start(struct upump *upump, bool status)
{
    struct upump_srt *upump_srt = upump_srt_from_upump(upump);
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(upump->mgr);

    switch (upump_srt->event) {
        case UPUMP_TYPE_IDLER:
            srt_mgr->idlers++;
            break;
        case UPUMP_TYPE_TIMER: {
            int events = SRT_EPOLL_IN;
            int err = srt_epoll_add_ssock(srt_mgr->epoll_id,
                                          upump_srt->fd, &events);
            if (err == SRT_ERROR)
                return;
            struct itimerspec timer;
            timer.it_value.tv_sec = upump_srt->timer.after / UCLOCK_FREQ;
            timer.it_value.tv_nsec = ((upump_srt->timer.after % UCLOCK_FREQ) *
                                     1000000000) / UCLOCK_FREQ;
            timer.it_interval.tv_sec = upump_srt->timer.repeat / UCLOCK_FREQ;
            timer.it_interval.tv_nsec = ((upump_srt->timer.repeat % UCLOCK_FREQ) *
                                         1000000000) / UCLOCK_FREQ;
            timerfd_settime(upump_srt->fd, 0, &timer, NULL);
            upump_srt->timer.expired = false;
            break;
        }
        case UPUMP_TYPE_FD_READ:
            if (upump_srt_lookup(upump->mgr, upump_srt->fd,
                                 UPUMP_TYPE_FD_WRITE)) {
                int events = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                int err = srt_epoll_update_ssock(srt_mgr->epoll_id,
                                                 upump_srt->fd, &events);
                if (err == SRT_ERROR)
                    return;
            } else {
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                int err = srt_epoll_add_ssock(srt_mgr->epoll_id,
                                              upump_srt->fd, &events);
                if (err == SRT_ERROR)
                    return;
            }
            break;
        case UPUMP_TYPE_FD_WRITE:
            if (upump_srt_lookup(upump->mgr, upump_srt->fd,
                                 UPUMP_TYPE_FD_READ)) {
                int events = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                int err = srt_epoll_update_ssock(srt_mgr->epoll_id,
                                                 upump_srt->fd, &events);
                if (err == SRT_ERROR)
                    return;
            } else {
                int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                int err = srt_epoll_add_ssock(srt_mgr->epoll_id,
                                              upump_srt->fd, &events);
                if (err == SRT_ERROR)
                    return;
            }
            break;
        case UPUMP_TYPE_SIGNAL: {
            int events = SRT_EPOLL_IN;
            int err = srt_epoll_add_ssock(srt_mgr->epoll_id,
                                          upump_srt->fd, &events);
            if (err == SRT_ERROR)
                return;
            break;
        }
        case UPUMP_SRT_TYPE_READ:
            if (upump_srt_lookup(upump->mgr, upump_srt->socket,
                                 UPUMP_SRT_TYPE_WRITE)) {
                int events = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                int err = srt_epoll_update_usock(srt_mgr->epoll_id,
                                                 upump_srt->socket, &events);
                if (err == SRT_ERROR)
                    return;
            } else {
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                int err = srt_epoll_add_usock(srt_mgr->epoll_id,
                                              upump_srt->socket, &events);
                if (err == SRT_ERROR)
                    return;
            }
            break;
        case UPUMP_SRT_TYPE_WRITE:
            if (upump_srt_lookup(upump->mgr, upump_srt->socket,
                                 UPUMP_SRT_TYPE_READ)) {
                int events = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                int err = srt_epoll_update_usock(srt_mgr->epoll_id,
                                                 upump_srt->socket, &events);
                if (err == SRT_ERROR)
                    return;
            } else {
                int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                int err = srt_epoll_add_usock(srt_mgr->epoll_id,
                                              upump_srt->socket, &events);
                if (err == SRT_ERROR)
                    return;
            }
            break;
    }
}

/** @This stops a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_srt_real_stop(struct upump *upump, bool status)
{
    struct upump_srt *upump_srt = upump_srt_from_upump(upump);
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(upump->mgr);

    switch (upump_srt->event) {
        case UPUMP_TYPE_IDLER:
            srt_mgr->idlers--;
            break;
        case UPUMP_TYPE_TIMER:
            srt_epoll_remove_ssock(srt_mgr->epoll_id, upump_srt->fd);
            struct itimerspec timer;
            timer.it_value.tv_sec = 0;
            timer.it_value.tv_nsec = 0;
            timer.it_interval.tv_sec = 0;
            timer.it_interval.tv_nsec = 0;
            timerfd_settime(upump_srt->fd, 0, &timer, NULL);
            break;
        case UPUMP_TYPE_FD_READ:
            if (upump_srt_lookup(upump->mgr, upump_srt->fd,
                                 UPUMP_TYPE_FD_WRITE)) {
                int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                srt_epoll_update_ssock(srt_mgr->epoll_id, upump_srt->fd,
                                       &events);
            } else
                srt_epoll_remove_ssock(srt_mgr->epoll_id, upump_srt->fd);
            break;
        case UPUMP_TYPE_FD_WRITE:
            if (upump_srt_lookup(upump->mgr, upump_srt->fd,
                                 UPUMP_TYPE_FD_READ)) {
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                srt_epoll_update_ssock(srt_mgr->epoll_id, upump_srt->fd,
                                       &events);
            } else
                srt_epoll_remove_ssock(srt_mgr->epoll_id, upump_srt->fd);
            break;
        case UPUMP_TYPE_SIGNAL:
            srt_epoll_remove_ssock(srt_mgr->epoll_id, upump_srt->fd);
            break;
        case UPUMP_SRT_TYPE_READ:
            if (upump_srt_lookup(upump->mgr, upump_srt->socket,
                                 UPUMP_SRT_TYPE_WRITE)) {
                int events = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
                srt_epoll_update_usock(srt_mgr->epoll_id, upump_srt->socket,
                                       &events);
            } else
                srt_epoll_remove_usock(srt_mgr->epoll_id, upump_srt->socket);
            break;
        case UPUMP_SRT_TYPE_WRITE:
            if (upump_srt_lookup(upump->mgr, upump_srt->socket,
                                 UPUMP_SRT_TYPE_READ)) {
                int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
                srt_epoll_update_usock(srt_mgr->epoll_id, upump_srt->socket,
                                       &events);
            } else
                srt_epoll_remove_usock(srt_mgr->epoll_id, upump_srt->socket);
            break;
    }
}

/** @This restarts a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status of the pump
 */
static void upump_srt_real_restart(struct upump *upump, bool status)
{
    struct upump_srt *upump_srt = upump_srt_from_upump(upump);
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(upump->mgr);

    switch (upump_srt->event) {
        case UPUMP_TYPE_TIMER: {
            struct itimerspec timer, prev;
            uint64_t value = upump_srt->timer.repeat ?: upump_srt->timer.after;
            timer.it_value.tv_sec = value / UCLOCK_FREQ;
            timer.it_value.tv_nsec = ((value % UCLOCK_FREQ) *
                                      1000000000) / UCLOCK_FREQ;
            timer.it_interval.tv_sec = upump_srt->timer.repeat / UCLOCK_FREQ;
            timer.it_interval.tv_nsec = ((upump_srt->timer.repeat % UCLOCK_FREQ) *
                                         1000000000) / UCLOCK_FREQ;
            timerfd_settime(upump_srt->fd, 0, &timer, &prev);
            upump_srt->timer.expired = false;
            if (prev.it_value.tv_sec == 0 && prev.it_value.tv_nsec == 0) {
                int events = SRT_EPOLL_IN;
                int err = srt_epoll_add_ssock(srt_mgr->epoll_id,
                                              upump_srt->fd, &events);
                if (err == SRT_ERROR)
                    return;
            }
            break;
        }
    }
}

/** @This releases the memory space previously used by a pump.
 *
 * @param upump description structure of the pump
 */
static void upump_srt_free(struct upump *upump)
{
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(upump->mgr);
    upump_stop(upump);
    upump_common_clean(upump);
    struct upump_srt *upump_srt = upump_srt_from_upump(upump);
    if (upump_srt->event == UPUMP_TYPE_TIMER ||
        upump_srt->event == UPUMP_TYPE_SIGNAL)
        close(upump_srt->fd);
    if (srt_mgr->running)
        upump_srt->free = true;
    else {
        ulist_delete(&upump_srt->uchain);
        upool_free(&srt_mgr->common_mgr.upump_pool, upump_srt);
    }
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to upump_srt or NULL in case of allocation error
 */
static void *upump_srt_alloc_inner(struct upool *upool)
{
    struct upump_common_mgr *common_mgr =
        upump_common_mgr_from_upump_pool(upool);
    struct upump_srt *upump_srt = malloc(sizeof(struct upump_srt));
    if (unlikely(upump_srt == NULL))
        return NULL;
    struct upump *upump = upump_srt_to_upump(upump_srt);
    upump->mgr = upump_common_mgr_to_upump_mgr(common_mgr);
    return upump_srt;
}

/** @internal @This frees a upump_srt.
 *
 * @param upool pointer to upool
 * @param upump_srt pointer to a upump_srt structure to free
 */
static void upump_srt_free_inner(struct upool *upool, void *upump_srt)
{
    free(upump_srt);
}

/** @This processes control commands on a upump_srt.
 *
 * @param upump description structure of the pump
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upump_srt_control(struct upump *upump, int command, va_list args)
{
    switch (command) {
        case UPUMP_START:
            upump_common_start(upump);
            return UBASE_ERR_NONE;
        case UPUMP_RESTART:
            upump_common_restart(upump);
            return UBASE_ERR_NONE;
        case UPUMP_STOP:
            upump_common_stop(upump);
            return UBASE_ERR_NONE;
        case UPUMP_FREE:
            upump_srt_free(upump);
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

/** @internal @This runs an event loop.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param mutex mutual exclusion primitives to access the event loop
 * @return an error code
 */
static int upump_srt_mgr_run(struct upump_mgr *mgr, struct umutex *mutex)
{
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_upump_mgr(mgr);

    if (mutex != NULL)
        return UBASE_ERR_INVALID;

    SRTSOCKET rfds[16];
    SRTSOCKET wfds[16];
    int lrfds[16];
    int lwfds[16];
    int blocking;

    do {
        int rnum = UBASE_ARRAY_SIZE(rfds);
        int wnum = UBASE_ARRAY_SIZE(wfds);
        int lrnum = UBASE_ARRAY_SIZE(lrfds);
        int lwnum = UBASE_ARRAY_SIZE(lwfds);
        int ret = srt_epoll_wait(srt_mgr->epoll_id, rfds, &rnum, wfds, &wnum,
                                 srt_mgr->idlers > 0 ? 0 : -1,
                                 lrfds, &lrnum, lwfds, &lwnum);

        bool dispatch_idlers = ret == 0 && srt_mgr->idlers > 0;

        if (ret == SRT_ERROR) {
            if (srt_getlasterror(NULL) != SRT_ETIMEOUT || srt_mgr->idlers == 0)
                return UBASE_ERR_EXTERNAL;
            dispatch_idlers = true;
        }

        srt_mgr->running = true;

        struct uchain *uchain;
        ulist_foreach(&srt_mgr->upumps, uchain) {
            struct upump_srt *upump_srt = upump_srt_from_uchain(uchain);
            struct upump *upump = upump_srt_to_upump(upump_srt);

            if (!upump_srt->common.started || upump_srt->free)
                continue;

            if (dispatch_idlers) {
                if (upump_srt->event == UPUMP_TYPE_IDLER)
                    upump_common_dispatch(upump);
                continue;
            }

            switch (upump_srt->event) {
                case UPUMP_TYPE_TIMER:
                case UPUMP_TYPE_SIGNAL:
                case UPUMP_TYPE_FD_READ:
                    for (int i = 0; i < lrnum; i++)
                        if (lrfds[i] == upump_srt->fd) {
                            if (upump_srt->event == UPUMP_TYPE_TIMER) {
                                uint64_t expirations;
                                if (read(upump_srt->fd, &expirations,
                                         sizeof (expirations)) == -1)
                                    break;
                                if (upump_srt->timer.repeat == 0)
                                    upump_srt->timer.expired = true;
                            } else if (upump_srt->event == UPUMP_TYPE_SIGNAL) {
                                struct signalfd_siginfo siginfo;
                                if (read(upump_srt->fd, &siginfo,
                                         sizeof (siginfo)) == -1)
                                    break;
                            }
                            upump_common_dispatch(upump);
                            break;
                        }
                    break;
                case UPUMP_TYPE_FD_WRITE:
                    for (int i = 0; i < lwnum; i++)
                        if (lwfds[i] == upump_srt->fd) {
                            upump_common_dispatch(upump);
                            break;
                        }
                    break;
                case UPUMP_SRT_TYPE_READ:
                    for (int i = 0; i < rnum; i++)
                        if (rfds[i] == upump_srt->socket) {
                            upump_common_dispatch(upump);
                            break;
                        }
                    break;
                case UPUMP_SRT_TYPE_WRITE:
                    for (int i = 0; i < wnum; i++)
                        if (wfds[i] == upump_srt->socket) {
                            upump_common_dispatch(upump);
                            break;
                        }
                    break;
            }
        }

        srt_mgr->running = false;

        struct uchain *uchain_tmp;
        ulist_delete_foreach(&srt_mgr->upumps, uchain, uchain_tmp) {
            struct upump_srt *upump_srt = upump_srt_from_uchain(uchain);
            if (upump_srt->free) {
                ulist_delete(&upump_srt->uchain);
                upool_free(&srt_mgr->common_mgr.upump_pool, upump_srt);
            }
        }

        blocking = 0;
        ulist_foreach(&srt_mgr->upumps, uchain) {
            struct upump_srt *upump_srt = upump_srt_from_uchain(uchain);
            if (upump_srt->common.started &&
                upump_srt->common.status)
                switch (upump_srt->event) {
                    case UPUMP_TYPE_TIMER:
                        if (upump_srt->timer.repeat > 0 ||
                            !upump_srt->timer.expired)
                        blocking++;
                        break;
                    default:
                        blocking++;
                }
        }
    } while (blocking > 0);

    return UBASE_ERR_NONE;
}

/** @This processes control commands on a upump_srt_mgr.
 *
 * @param mgr pointer to a upump_mgr structure
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upump_srt_mgr_control(struct upump_mgr *mgr,
                                int command, va_list args)
{
    switch (command) {
        case UPUMP_MGR_RUN: {
            struct umutex *mutex = va_arg(args, struct umutex *);
            return upump_srt_mgr_run(mgr, mutex);
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
static void upump_srt_mgr_free(struct urefcount *urefcount)
{
    struct upump_srt_mgr *srt_mgr = upump_srt_mgr_from_urefcount(urefcount);
    upump_common_mgr_clean(upump_srt_mgr_to_upump_mgr(srt_mgr));
    srt_epoll_release(srt_mgr->epoll_id);
    free(srt_mgr);
}

/** @This allocates and initializes a upump_srt_mgr structure.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_srt_mgr_alloc(uint16_t upump_pool_depth,
                                      uint16_t upump_blocker_pool_depth)
{
    struct upump_srt_mgr *srt_mgr =
        malloc(sizeof(struct upump_srt_mgr) +
               upump_common_mgr_sizeof(upump_pool_depth,
                                       upump_blocker_pool_depth));
    if (unlikely(srt_mgr == NULL))
        return NULL;

    srt_mgr->epoll_id = srt_epoll_create();
    if (unlikely(srt_mgr->epoll_id == SRT_ERROR)) {
        free(srt_mgr);
        return NULL;
    }
    srt_epoll_set(srt_mgr->epoll_id, SRT_EPOLL_ENABLE_EMPTY);

    struct upump_mgr *mgr = upump_srt_mgr_to_upump_mgr(srt_mgr);
    mgr->signature = UPUMP_SRT_SIGNATURE;
    urefcount_init(upump_srt_mgr_to_urefcount(srt_mgr), upump_srt_mgr_free);
    srt_mgr->common_mgr.mgr.refcount = upump_srt_mgr_to_urefcount(srt_mgr);
    srt_mgr->common_mgr.mgr.upump_alloc = upump_srt_alloc;
    srt_mgr->common_mgr.mgr.upump_control = upump_srt_control;
    srt_mgr->common_mgr.mgr.upump_mgr_control = upump_srt_mgr_control;
    upump_common_mgr_init(mgr, upump_pool_depth, upump_blocker_pool_depth,
                          srt_mgr->upool_extra,
                          upump_srt_real_start, upump_srt_real_stop,
                          upump_srt_real_restart,
                          upump_srt_alloc_inner, upump_srt_free_inner);

    ulist_init(&srt_mgr->upumps);
    srt_mgr->idlers = 0;
    srt_mgr->running = false;
    return mgr;
}

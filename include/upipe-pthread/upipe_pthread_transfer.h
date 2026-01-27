/*
 * Copyright (C) 2014-2019 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module allowing to transfer other pipes to a new POSIX thread
 * This is particularly helpful for multithreaded applications.
 */

#ifndef _UPIPE_PTHREAD_UPIPE_PTHREAD_TRANSFER_H_
/** @hidden */
#define _UPIPE_PTHREAD_UPIPE_PTHREAD_TRANSFER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uprobe.h"
#include "upipe/upump.h"

#include <stdint.h>
#include <pthread.h>

/** @hidden */
struct umutex;

/** @This returns a management structure for transfer pipes, using a new
 * pthread. You would need one management structure per target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc alloc function provided by the upump manager
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param mutex mutual exclusion pimitives to access the event loop, or NULL
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @return pointer to xfer manager
 */
struct upipe_mgr *upipe_pthread_xfer_mgr_alloc(uint8_t queue_length,
        uint16_t msg_pool_depth, struct uprobe *uprobe_pthread_upump_mgr,
        upump_mgr_alloc upump_mgr_alloc, uint16_t upump_pool_depth,
        uint16_t upump_blocker_pool_depth, struct umutex *mutex,
        pthread_t *pthread_id_p, const pthread_attr_t *restrict attr);

/** @This returns a management structure for transfer pipes, using a new
 * pthread with a custom name. You would need one management structure per
 * target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc alloc function provided by the upump manager
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param mutex mutual exclusion pimitives to access the event loop, or NULL
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @param name custom name
 * @return pointer to xfer manager
 */
struct upipe_mgr *upipe_pthread_xfer_mgr_alloc_named(uint8_t queue_length,
        uint16_t msg_pool_depth, struct uprobe *uprobe_pthread_upump_mgr,
        upump_mgr_alloc upump_mgr_alloc, uint16_t upump_pool_depth,
        uint16_t upump_blocker_pool_depth, struct umutex *mutex,
        pthread_t *pthread_id_p, const pthread_attr_t *restrict attr,
        const char *name);

/** @This returns a management structure for transfer pipes, using a new
 * pthread with a printf-style custom name. You would need one management
 * structure per target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc alloc function provided by the upump manager
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param mutex mutual exclusion pimitives to access the event loop, or NULL
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @param format format of the custom name, followed by optional arguments
 * @return pointer to xfer manager
 */
UBASE_FMT_PRINTF(10, 11)
static inline struct upipe_mgr *upipe_pthread_xfer_mgr_alloc_named_va(
        uint8_t queue_length, uint16_t msg_pool_depth,
        struct uprobe *uprobe_pthread_upump_mgr,
        upump_mgr_alloc upump_mgr_alloc, uint16_t upump_pool_depth,
        uint16_t upump_blocker_pool_depth, struct umutex *mutex,
        pthread_t *pthread_id_p, const pthread_attr_t *restrict attr,
        const char *format, ...)
{
    UBASE_VARARG(upipe_pthread_xfer_mgr_alloc_named(queue_length,
                                                    msg_pool_depth,
                                                    uprobe_pthread_upump_mgr,
                                                    upump_mgr_alloc,
                                                    upump_pool_depth,
                                                    upump_blocker_pool_depth,
                                                    mutex, pthread_id_p, attr,
                                                    string), NULL)
}

/** @This returns a management structure for transfer pipes, using a new
 * pthread. You would need one management structure per target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc alloc function provided by the upump manager
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param mutex mutual exclusion pimitives to access the event loop, or NULL
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @param priority priority of the thread or INT_MAX to leave it unchanged
 * @return pointer to xfer manager
 */
struct upipe_mgr *upipe_pthread_xfer_mgr_alloc_prio(
    uint8_t queue_length, uint16_t msg_pool_depth,
    struct uprobe *uprobe_pthread_upump_mgr,
    upump_mgr_alloc upump_mgr_alloc, uint16_t upump_pool_depth,
    uint16_t upump_blocker_pool_depth, struct umutex *mutex,
    pthread_t *pthread_id_p, const pthread_attr_t *restrict attr,
    int priority);

/** @This returns a management structure for transfer pipes, using a new
 * pthread. You would need one management structure per target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc alloc function provided by the upump manager
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param mutex mutual exclusion pimitives to access the event loop, or NULL
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @param priority priority of the thread or INT_MAX to leave it unchanged
 * @param name custom name or NULL
 * @return pointer to xfer manager
 */
struct upipe_mgr *upipe_pthread_xfer_mgr_alloc_prio_named(
    uint8_t queue_length, uint16_t msg_pool_depth,
    struct uprobe *uprobe_pthread_upump_mgr,
    upump_mgr_alloc upump_mgr_alloc, uint16_t upump_pool_depth,
    uint16_t upump_blocker_pool_depth, struct umutex *mutex,
    pthread_t *pthread_id_p, const pthread_attr_t *restrict attr,
    int priority, const char *name);

/** @This returns a management structure for transfer pipes, using a new
 * pthread with a printf-style custom name. You would need one management
 * structure per target thread.
 *
 * @param queue_length maximum length of the internal queue of commands
 * @param msg_pool_depth maximum number of messages in the pool
 * @param uprobe_pthread_upump_mgr pointer to optional probe, that will be set
 * with the created upump_mgr
 * @param upump_mgr_alloc alloc function provided by the upump manager
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param mutex mutual exclusion pimitives to access the event loop, or NULL
 * @param pthread_id_p reference to created thread ID (may be NULL)
 * @param attr pthread attributes
 * @param priority priority of the thread or INT_MAX to leave it unchanged
 * @param format format of the custom name, followed by optional arguments
 * @return pointer to xfer manager
 */
UBASE_FMT_PRINTF(11, 12)
static inline struct upipe_mgr *upipe_pthread_xfer_mgr_alloc_prio_named_va(
        uint8_t queue_length, uint16_t msg_pool_depth,
        struct uprobe *uprobe_pthread_upump_mgr,
        upump_mgr_alloc upump_mgr_alloc, uint16_t upump_pool_depth,
        uint16_t upump_blocker_pool_depth, struct umutex *mutex,
        pthread_t *pthread_id_p, const pthread_attr_t *restrict attr,
        int priority, const char *format, ...)
{
    UBASE_VARARG(upipe_pthread_xfer_mgr_alloc_prio_named(
            queue_length,
            msg_pool_depth,
            uprobe_pthread_upump_mgr,
            upump_mgr_alloc,
            upump_pool_depth,
            upump_blocker_pool_depth,
            mutex, pthread_id_p, attr,
            priority, string), NULL)
}

#ifdef __cplusplus
}
#endif
#endif

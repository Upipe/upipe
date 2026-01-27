/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short common declarations for event loop handlers
 */

#ifndef _UPIPE_UPUMP_COMMON_H_
/** @hidden */
#define _UPIPE_UPUMP_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/ulist.h"
#include "upipe/upool.h"
#include "upipe/upump.h"

#include <stdbool.h>
#include <stdarg.h>

/** @hidden */
struct upump_blocker;

/** @This stores upump parameters invisible from modules but usually common.
 */
struct upump_common {
    /** true if upump_start() was called on the pump */
    bool started;
    /** true if the pump is blocking */
    bool status;
    /** list of blockers registered on this pump */
    struct uchain blockers;

    /** public upump structure */
    struct upump upump;
};

UBASE_FROM_TO(upump_common, upump, upump, upump)

/** @This allocates and initializes a blocker.
 *
 * @param upump description structure of the pump
 * @return pointer to blocker
 */
struct upump_blocker *upump_common_blocker_alloc(struct upump *upump);

/** @This releases a blocker, and if allowed restarts the pump.
 *
 * @param blocker description structure of the blocker
 */
void upump_common_blocker_free(struct upump_blocker *blocker);

/** @This initializes the common part of a pump.
 *
 * @param upump description structure of the pump
 */
void upump_common_init(struct upump *upump);

/** @This dispatches a pump.
 *
 * @param upump description structure of the pump
 */
void upump_common_dispatch(struct upump *upump);

/** @This starts a pump if allowed.
 *
 * @param upump description structure of the pump
 */
void upump_common_start(struct upump *upump);

/** @This restarts a pump if allowed.
 *
 * @param upump description structure of the pump
 */
void upump_common_restart(struct upump *upump);

/** @This stops a pump if needed.
 *
 * @param upump description structure of the pump
 */
void upump_common_stop(struct upump *upump);

/** @This gets the blocking status of a pump.
 *
 * @param upump description structure of the pump
 * @param status_p reference to blocking status
 */
void upump_common_get_status(struct upump *upump, int *status_p);

/** @This sets the blocking status of a pump.
 *
 * @param upump description structure of the pump
 * @param status blocking status
 */
void upump_common_set_status(struct upump *upump, int status);

/** @This cleans up the common part of a pump.
 *
 * @param upump description structure of the pump
 */
void upump_common_clean(struct upump *upump);

/** @This stores management parameters invisible from modules but usually
 * common.
 */
struct upump_common_mgr {
    /** upump pool */
    struct upool upump_pool;
    /** upump_blocker_pool */
    struct upool upump_blocker_pool;

    /** function to really start a watcher */
    void (*upump_real_start)(struct upump *, bool);
    /** function to really restart a watcher */
    void (*upump_real_restart)(struct upump *, bool);
    /** function to really stop a watcher */
    void (*upump_real_stop)(struct upump *, bool);

    /** structure exported to modules */
    struct upump_mgr mgr;
};

UBASE_FROM_TO(upump_common_mgr, upump_mgr, upump_mgr, mgr)
UBASE_FROM_TO(upump_common_mgr, upool, upump_pool, upump_pool)
UBASE_FROM_TO(upump_common_mgr, upool, upump_blocker_pool, upump_blocker_pool)

/** @This instructs an existing manager to release all structures
 * currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_vacuum(struct upump_mgr *mgr);

/** @This returns the extra buffer space needed for pools.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 */
size_t upump_common_mgr_sizeof(uint16_t upump_pool_depth,
                               uint16_t upump_blocker_pool_depth);

/** @This cleans up the common parts of a upump_common_mgr structure.
 * Note that all pumps have to be stopped before.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 */
void upump_common_mgr_clean(struct upump_mgr *mgr);

/** @This initializes the common parts of a upump_common_mgr structure.
 *
 * @param mgr pointer to a upump_mgr structure wrapped into a
 * upump_common_mgr structure
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @param pool_extra extra buffer space (see @ref upump_common_mgr_sizeof)
 * @param upump_real_start function of the real manager that starts a pump
 * @param upump_real_stop function of the real manager that stops a pump
 * @param upump_real_restart function of the real manager that restarts a pump
 * @param upump_alloc_inner function to call to allocate a upump buffer
 * @param upump_free_inner function to call to release a upump buffer
 */
void upump_common_mgr_init(struct upump_mgr *mgr,
                           uint16_t upump_pool_depth,
                           uint16_t upump_blocker_pool_depth, void *pool_extra,
                           void (*upump_real_start)(struct upump *, bool),
                           void (*upump_real_stop)(struct upump *, bool),
                           void (*upump_real_restart)(struct upump *, bool),
                           void *(*upump_alloc_inner)(struct upool *),
                           void (*upump_free_inner)(struct upool *, void *));

#ifdef __cplusplus
}
#endif
#endif

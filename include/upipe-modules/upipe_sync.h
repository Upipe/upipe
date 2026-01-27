/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe sync module - synchronize streams for muxing
 */

#ifndef _UPIPE_MODULES_UPIPE_SYNC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SYNC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SYNC_SIGNATURE UBASE_FOURCC('s', 'y', 'n', 'c')
#define UPIPE_SYNC_SUB_SIGNATURE UBASE_FOURCC('s', 'y', 'n', 's')

/** @This extends uprobe_event with specific events for sync. */
enum uprobe_sync_event {
    UPROBE_SYNC_SENTINEL = UPROBE_LOCAL,

    /** received picture event (unsigned int got_picture) */
    UPROBE_SYNC_PICTURE
};

/** @This returns the management structure for sync pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sync_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

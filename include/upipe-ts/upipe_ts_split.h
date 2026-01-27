/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module splitting PIDs of a transport stream
 */

#ifndef _UPIPE_TS_UPIPE_TS_SPLIT_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SPLIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_SPLIT_SIGNATURE UBASE_FOURCC('t','s','<',' ')
#define UPIPE_TS_SPLIT_OUTPUT_SIGNATURE UBASE_FOURCC('t','s','<','o')

/** @This extends uprobe_event with specific events for ts split. */
enum uprobe_ts_split_event {
    UPROBE_TS_SPLIT_SENTINEL = UPROBE_TS_DEMUX_SPLIT,

    /** the given PID is needed for correct operation (unsigned int) */
    UPROBE_TS_SPLIT_ADD_PID,
    /** the given PID is no longer needed (unsigned int) */
    UPROBE_TS_SPLIT_DEL_PID
};

/** @This returns the management structure for all ts_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_split_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

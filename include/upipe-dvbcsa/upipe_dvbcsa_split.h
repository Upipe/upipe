/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file @short Upipe module that notifies for all known ES PIDs.
 */
#ifndef _UPIPE_DVBCSA_UPIPE_DVBCSA_SPLIT_H_
#define _UPIPE_DVBCSA_UPIPE_DVBCSA_SPLIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"

#define UPIPE_DVBCSA_SPLIT_SIGNATURE    UBASE_FOURCC('c','s','a','s')

/** @This enumerates the privates events for dvbcsa pipes. */
enum uprobe_dvbcsa_split_event {
    /** sentinel */
    UPROBE_DVBCSA_SPLIT_SENTINEL = UPROBE_LOCAL,

    /** pid added (uint64_t) */
    UPROBE_DVBCSA_SPLIT_ADD_PID,
    /** pid removed (uint64_t) */
    UPROBE_DVBCSA_SPLIT_DEL_PID,
};

/** @This returns the dvbcsa split pipe manager.
 *
 * @return a pointer to the manager
 */
struct upipe_mgr *upipe_dvbcsa_split_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

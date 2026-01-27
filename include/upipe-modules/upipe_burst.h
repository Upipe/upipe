/*
 * Copyright (C) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe burst module
 *
 * The burst pipe makes sure that an entire block is read without blocking
 * the pump.
 */

#ifndef _UPIPE_MODULES_UPIPE_BURST_H_
/** @hidden */
# define _UPIPE_MODULES_UPIPE_BURST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_BURST_SIGNATURE UBASE_FOURCC('b','u','r','s')

/** @This extends @ref uprobe_event with specific burst events. */
enum uprobe_burst_event {
    UPROBE_BURST_SENTINEL = UPROBE_LOCAL,

    /** burst pipe blocked state change (bool) */
    UPROBE_BURST_UPDATE,
};

/** @This converts @ref uprobe_burst_event to a string.
 *
 * @param event event to convert
 * @return a string of NULL if invalid
 */
static inline const char *upipe_burst_event_str(int event)
{
    switch ((enum uprobe_burst_event)event) {
    UBASE_CASE_TO_STR(UPROBE_BURST_UPDATE);
    case UPROBE_BURST_SENTINEL: break;
    }
    return NULL;
}

/** @This allocates a burst pipe manager. */
struct upipe_mgr *upipe_burst_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_BURST_H_ */

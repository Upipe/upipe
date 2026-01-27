/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe catching need_upump_mgr events and providing a given upump manager
 */

#ifndef _UPIPE_UPROBE_UPUMP_MGR_H_
/** @hidden */
#define _UPIPE_UPROBE_UPUMP_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

#include <stdbool.h>

/** @hidden */
struct upump_mgr;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_upump_mgr {
    /** pointer to upump_mgr to provide */
    struct upump_mgr *upump_mgr;
    /** true if the probe is frozen on this thread */
    bool frozen;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_upump_mgr, uprobe);

/** @This initializes an already allocated uprobe_upump_mgr structure.
 *
 * @param uprobe_upump_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param upump_mgr upump manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_upump_mgr_init(struct uprobe_upump_mgr *uprobe_upump_mgr,
                                     struct uprobe *next,
                                     struct upump_mgr *upump_mgr);

/** @This cleans a uprobe_upump_mgr structure.
 *
 * @param uprobe_upump_mgr structure to clean
 */
void uprobe_upump_mgr_clean(struct uprobe_upump_mgr *uprobe_upump_mgr);

/** @This allocates a new uprobe_upump_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param upump_mgr upump manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_upump_mgr_alloc(struct uprobe *next,
                                      struct upump_mgr *upump_mgr);

/** @This changes the upump_mgr set by this probe.
 *
 * @param uprobe pointer to probe
 * @param upump_mgr new upump manager to provide to pipes
 */
void uprobe_upump_mgr_set(struct uprobe *uprobe, struct upump_mgr *upump_mgr);

#ifdef __cplusplus
}
#endif
#endif

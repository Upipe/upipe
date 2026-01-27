/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe catching provide_request events asking for a uclock
 */

#ifndef _UPIPE_UPROBE_UCLOCK_H_
/** @hidden */
#define _UPIPE_UPROBE_UCLOCK_H_

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @hidden */
struct uclock;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_uclock {
    /** pointer to uclock to provide */
    struct uclock *uclock;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_uclock, uprobe)

/** @This initializes an already allocated uprobe_uclock structure.
 *
 * @param uprobe_uclock pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param uclock uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uclock_init(struct uprobe_uclock *uprobe_uclock,
                                  struct uprobe *next,
                                  struct uclock *uclock);

/** @This cleans a uprobe_uclock structure.
 *
 * @param uprobe_uclock structure to clean
 */
void uprobe_uclock_clean(struct uprobe_uclock *uprobe_uclock);

/** @This allocates a new uprobe_uclock structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param uclock uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uclock_alloc(struct uprobe *next, struct uclock *uclock);

/** @This changes the uclock set by this probe.
 *
 * @param uprobe pointer to probe
 * @param uclock new uref manager to provide to pipes
 */
void uprobe_uclock_set(struct uprobe *uprobe, struct uclock *uclock);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short probe asserting that all events come from the same thread
 */

#ifndef _UPIPE_PTHREAD_UPROBE_PTHREAD_ASSERT_H_
/** @hidden */
#define _UPIPE_PTHREAD_UPROBE_PTHREAD_ASSERT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"
#include "upipe/uprobe_helper_uprobe.h"

#include <pthread.h>

/** @hidden */
struct upump_mgr;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_pthread_assert {
    /** true if the pthread ID was inited */
    bool inited;
    /** asserted pthread ID */
    pthread_t thread_id;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_pthread_assert, uprobe);

/** @This initializes an already allocated uprobe_pthread_assert structure.
 *
 * @param uprobe_pthread_assert pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pthread_assert_init(
        struct uprobe_pthread_assert *uprobe_pthread_assert,
        struct uprobe *next);

/** @This cleans a uprobe_pthread_assert structure.
 *
 * @param uprobe_pthread_assert structure to clean
 */
void uprobe_pthread_assert_clean(struct uprobe_pthread_assert *uprobe_pthread_assert);

/** @This allocates a new uprobe_pthread_assert structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pthread_assert_alloc(struct uprobe *next);

/** @This changes the thread ID used by this probe to the current thread.
 *
 * @param uprobe pointer to probe
 * @param thread_id thread ID
 * @return an error code
 */
int uprobe_pthread_assert_set(struct uprobe *uprobe, pthread_t thread_id);

#ifdef __cplusplus
}
#endif
#endif

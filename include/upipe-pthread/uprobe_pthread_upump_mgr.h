/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short probe catching need_upump_mgr events and providing a upump manager based on thread local storage
 */

#ifndef _UPIPE_PTHREAD_UPROBE_PTHREAD_UPUMP_MGR_H_
/** @hidden */
#define _UPIPE_PTHREAD_UPROBE_PTHREAD_UPUMP_MGR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#include <pthread.h>

/** @hidden */
struct upump_mgr;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_pthread_upump_mgr {
    /** pthread key pointing to thread local storage */
    pthread_key_t key;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_pthread_upump_mgr, uprobe);

/** @This initializes an already allocated uprobe_pthread_upump_mgr structure.
 *
 * @param uprobe_pthread_upump_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pthread_upump_mgr_init(
        struct uprobe_pthread_upump_mgr *uprobe_pthread_upump_mgr,
        struct uprobe *next);

/** @This cleans a uprobe_pthread_upump_mgr structure.
 *
 * @param uprobe_pthread_upump_mgr structure to clean
 */
void uprobe_pthread_upump_mgr_clean(struct uprobe_pthread_upump_mgr *uprobe_pthread_upump_mgr);

/** @This allocates a new uprobe_pthread_upump_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pthread_upump_mgr_alloc(struct uprobe *next);

/** @This changes the upump_mgr set by this probe for the current thread.
 *
 * @param uprobe pointer to probe
 * @param upump_mgr new upump manager to provide to pipes
 * @return an error code
 */
int uprobe_pthread_upump_mgr_set(struct uprobe *uprobe,
                                 struct upump_mgr *upump_mgr);

#ifdef __cplusplus
}
#endif
#endif

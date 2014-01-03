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
 * @short probe catching need_uclock events and providing a given uclock
 */

#ifndef _UPIPE_UPROBE_UCLOCK_H_
/** @hidden */
#define _UPIPE_UPROBE_UCLOCK_H_

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

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

#endif

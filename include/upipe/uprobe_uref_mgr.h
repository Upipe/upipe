/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short probe catching provide_request events asking for a uref manager
 */

#ifndef _UPIPE_UPROBE_UREF_MGR_H_
/** @hidden */
#define _UPIPE_UPROBE_UREF_MGR_H_

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @hidden */
struct uref_mgr;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_uref_mgr {
    /** pointer to uref_mgr to provide */
    struct uref_mgr *uref_mgr;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_uref_mgr, uprobe)

/** @This initializes an already allocated uprobe_uref_mgr structure.
 *
 * @param uprobe_uref_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param uref_mgr uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uref_mgr_init(struct uprobe_uref_mgr *uprobe_uref_mgr,
                                    struct uprobe *next,
                                    struct uref_mgr *uref_mgr);

/** @This cleans a uprobe_uref_mgr structure.
 *
 * @param uprobe_uref_mgr structure to clean
 */
void uprobe_uref_mgr_clean(struct uprobe_uref_mgr *uprobe_uref_mgr);

/** @This allocates a new uprobe_uref_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param uref_mgr uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uref_mgr_alloc(struct uprobe *next,
                                     struct uref_mgr *uref_mgr);

/** @This changes the uref_mgr set by this probe.
 *
 * @param uprobe pointer to probe
 * @param uref_mgr new uref manager to provide to pipes
 */
void uprobe_uref_mgr_set(struct uprobe *uprobe, struct uref_mgr *uref_mgr);

#ifdef __cplusplus
}
#endif
#endif

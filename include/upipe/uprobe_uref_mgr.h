/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short probe catching need_uref_mgr events and providing a given uref
 * manager
 */

#ifndef _UPIPE_UPROBE_UREF_MGR_H_
/** @hidden */
#define _UPIPE_UPROBE_UREF_MGR_H_

#include <upipe/uprobe.h>

/** @hidden */
struct uref_mgr;

/** @This allocates a new uprobe_uref_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param uref_mgr uref manager to provide to pipes
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_uref_mgr_alloc(struct uprobe *next,
                                     struct uref_mgr *uref_mgr);

/** @This frees a uprobe_uref_mgr structure.
 *
 * @param uprobe structure to free
 */
void uprobe_uref_mgr_free(struct uprobe *uprobe);

/** @This changes the uref_mgr set by this probe.
 *
 * @param uprobe pointer to probe
 * @param uref_mgr new uref manager to provide to pipes
 */
void uprobe_uref_mgr_set(struct uprobe *next, struct uref_mgr *uref_mgr);

#endif

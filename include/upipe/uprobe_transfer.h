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
 * @short probe transferring events from one thread to another
 */

#ifndef _UPIPE_UPROBE_TRANSFER_H_
/** @hidden */
#define _UPIPE_UPROBE_TRANSFER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

#include <stdbool.h>

#define UPROBE_XFER_SIGNATURE UBASE_FOURCC('x','f','e','r')

/** additional event types */
enum uprobe_xfer_event {
    UPROBE_XFER_SENTINEL = UPROBE_LOCAL + UPROBE_LOCAL,

    /** a void event needs to be transferred */
    UPROBE_XFER_VOID,
    /** a uint64_t event needs to be transferred */
    UPROBE_XFER_UINT64_T,
    /** a local unsigned long event needs to be transferred */
    UPROBE_XFER_UNSIGNED_LONG_LOCAL
};

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_xfer {
    /** list of events to transfer */
    struct uchain subs;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_xfer, uprobe);

/** @This initializes an already allocated uprobe_xfer structure.
 *
 * @param uprobe_xfer pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_xfer_init(struct uprobe_xfer *uprobe_xfer,
                                struct uprobe *next);

/** @This cleans a uprobe_xfer structure.
 *
 * @param uprobe_xfer structure to clean
 */
void uprobe_xfer_clean(struct uprobe_xfer *uprobe_xfer);

/** @This allocates a new uprobe_xfer structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_xfer_alloc(struct uprobe *next);

/** @This allocates a sub-structure to transfer an event.
 *
 * @param uprobe pointer to probe
 * @param xfer_event type of the event
 * @param event event to be transferred
 * @param signature event signature, or 0 for standard events
 * @return an error code
 */
int uprobe_xfer_add(struct uprobe *uprobe, enum uprobe_xfer_event xfer_event,
                    int event, uint32_t signature);

#ifdef __cplusplus
}
#endif
#endif

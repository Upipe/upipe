/*****************************************************************************
 * uprobe.h: structure used to raise events
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UPROBE_H_
/** @hidden */
#define _UPIPE_UPROBE_H_

#include <upipe/ubase.h>

#include <stdbool.h>
#include <stdarg.h>

/** @hidden */
struct uprobe;
/** @hidden */
struct upipe;

/** common types of events */
enum uprobe_event {
    /** an allocation error occurred, data may be lost (void) */
    UPROBE_AERROR,
    /** a upump error occurred, a watcher couldn't be created, the
     * application will not run properly (void) */
    UPROBE_UPUMP_ERROR,
    /** unable to read from an input because the end of file was reached, or
     * because of an error (const char *) */
    UPROBE_READ_END,
    /** unable to write to an output because the disk is full or another error
     * occurred (const char *) */
    UPROBE_WRITE_END,
    /** a demux pipe declares a new output flow (const char *,
     * struct uchain *) */
    UPROBE_NEW_FLOW,
    /** a uref manager is necessary to operate (void) */
    UPROBE_NEED_UREF_MGR,
    /** a upump manager is necessary to operate (void) */
    UPROBE_NEED_UPUMP_MGR
};

/** @This is the call-back type for uprobe events; it returns true if the
 * event was handled. */
typedef bool (*uprobe_throw)(struct uprobe *, struct upipe *,
                             enum uprobe_event, va_list);

/** @This is a structure passed to a module upon initializing a new pipe. */
struct uprobe {
    /** function to throw events */
    uprobe_throw uthrow;
    /** next probe to test if this one doesn't catch the event */
    struct uprobe *next;
};

/** @This initializes a uprobe structure. It is typically called by the
 * application or a pipe creating sub-pipes (on a structure already
 * allocated by the master object).
 *
 * @param uprobe pointer to probe
 * @param uthrow function which will be called when an event is thrown
 * @param next next probe to test if this one doesn't catch the event
 */
static inline void uprobe_init(struct uprobe *uprobe, uprobe_throw uthrow,
                               struct uprobe *next)
{
    uprobe->uthrow = uthrow;
    uprobe->next = next;
}

#endif

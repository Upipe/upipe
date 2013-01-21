/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short Upipe structure used to raise events from pipes
 */

#ifndef _UPIPE_UPROBE_H_
/** @hidden */
#define _UPIPE_UPROBE_H_

#include <upipe/ubase.h>
#include <upipe/uref_flow.h>

#include <stdbool.h>
#include <stdarg.h>

/** @hidden */
struct uprobe;
/** @hidden */
struct upipe;

/** common types of events */
enum uprobe_event {
    /** a pipe is ready to process data and respond to control commands
     * asking for info about the processing (void) */
    UPROBE_READY,
    /** a pipe is about to be destroyed (void) */
    UPROBE_DEAD,
    /** an allocation error occurred, data may be lost (void); from now on
     * the behaviour of the pipe is undefined, except @ref upipe_release */
    UPROBE_AERROR,
    /** the last flow definition sent is not compatible with this pipe
     * (struct uref *) */
    UPROBE_FLOW_DEF_ERROR,
    /** a upump error occurred, a watcher couldn't be created, the
     * application will not run properly (void); from now on the behaviour of
     * the pipe is undefined, except @ref upipe_release */
    UPROBE_UPUMP_ERROR,
    /** unable to read from an input because the end of file was reached, or
     * because of an error (const char *) */
    UPROBE_READ_END,
    /** unable to write to an output because the disk is full or another error
     * occurred (const char *) */
    UPROBE_WRITE_END,
    /** a uref manager is necessary to operate (void) */
    UPROBE_NEED_UREF_MGR,
    /** a upump manager is necessary to operate (void) */
    UPROBE_NEED_UPUMP_MGR,
    /** an output pipe is necessary to operate (struct uref *) */
    UPROBE_NEED_OUTPUT,
    /** a ubuf manager is necessary to operate (struct uref *) */
    UPROBE_NEED_UBUF_MGR,
    /** a split pipe declares a new possible output flow (struct uref *) */
    UPROBE_SPLIT_NEW_FLOW,
    /** a pipe got synchronized with its input (void) */
    UPROBE_SYNC_ACQUIRED,
    /** a pipe lost synchronization with its input (void) */
    UPROBE_SYNC_LOST,

    /** non-standard events implemented by a module type can start from
     * there (first arg = signature) */
    UPROBE_LOCAL = 0x8000
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

/** @This implements the common parts of a plumber probe (catching the
 * need_output event).
 *
 * @param uprobe pointer to the probe
 * @param upipe pointer to the pipe
 * @param event event triggered by the pipe
 * @param args arguments of the event
 * @param flow_def_p filled in with the flow definition uref passed with the
 * event
 * @param def_p filled in with the flow definition
 * @return false if the event cannot be handled by a plumber
 */
static inline bool uprobe_plumber(struct uprobe *uprobe, struct upipe *upipe,
                                  enum uprobe_event event, va_list args,
                                  struct uref **flow_def_p, const char **def_p)
{
    if (event != UPROBE_NEED_OUTPUT)
        return false;

    va_list args_copy;
    va_copy(args_copy, args);
    *flow_def_p = va_arg(args, struct uref *);
    va_end(args_copy);

    if (unlikely(!uref_flow_get_def(*flow_def_p, def_p)))
        return false;

    return true;
}

#endif

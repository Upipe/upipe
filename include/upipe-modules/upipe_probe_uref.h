/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe module - probe uref
 * This linear module sends a probe for each uref. It can also
 * drop uref on demand using the second probe va_arg element.
 */

#ifndef _UPIPE_MODULES_UPIPE_PROBE_UREF_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PROBE_UREF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/upipe.h"

#define UPIPE_PROBE_UREF_SIGNATURE UBASE_FOURCC('p','r','b','u')

/** @This extends uprobe_event with specific events for probe_uref. */
enum uprobe_probe_uref_event {
    UPROBE_PROBE_SENTINEL = UPROBE_LOCAL,

    /** received uref event (struct uref *, struct upump **, bool *drop) */
    UPROBE_PROBE_UREF
};

/** @This defines a helper to check probe_uref extended events. */
#define uprobe_probe_uref_check_extended(event, args, expected_event)   \
    uprobe_check_extended(event, args, expected_event,                  \
                          UPIPE_PROBE_UREF_SIGNATURE)

/** @This checks if an event is the extended probe_uref event.
 *
 * @param event event triggered by the pipe
 * @param args arguments of the event
 * @param uref_p filled with the first argument
 * @param upump_pp filled with the second argument
 * @param drop_p filled with the third argument
 * @return true if the event is the expected extended event, false otherwise
 */
static inline bool uprobe_probe_uref_check(int event, va_list args,
                                           struct uref **uref_p,
                                           struct upump ***upump_pp,
                                           bool **drop_p)
{
    if (uprobe_probe_uref_check_extended(event, args, UPROBE_PROBE_UREF)) {
        struct uref *uref = va_arg(args, struct uref *);
        struct upump **upump_p = va_arg(args, struct upump **);
        bool *drop = va_arg(args, bool *);
        if (uref_p)
            *uref_p = uref;
        if (upump_pp)
            *upump_pp = upump_p;
        if (drop_p)
            *drop_p = drop;
        return true;
    }
    return false;
}

/** @This returns the management structure for probe pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_probe_uref_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

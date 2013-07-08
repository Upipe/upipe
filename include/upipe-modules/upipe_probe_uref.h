/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * This linear module sends a probe for each uref.
 */

#ifndef _UPIPE_MODULES_UPIPE_PROBE_UREF_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PROBE_UREF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_PROBE_UREF_SIGNATURE UBASE_FOURCC('p','r','b','u')

/** @This extends uprobe_event with specific events for probe_uref. */
enum uprobe_probe_uref_event {
    UPROBE_PROBE_SENTINEL = UPROBE_LOCAL,

    /** rotate event (struct uref *uref) */
    UPROBE_PROBE_UREF
};

/** @This returns the management structure for probe pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_probe_uref_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

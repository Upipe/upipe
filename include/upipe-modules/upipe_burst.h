/*
 * Copyright (C) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short Upipe burst module
 *
 * The burst pipe makes sure that an entire block is read without blocking
 * the pump.
 */

#ifndef _UPIPE_MODULES_UPIPE_BURST_H_
/** @hidden */
# define _UPIPE_MODULES_UPIPE_BURST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_BURST_SIGNATURE UBASE_FOURCC('b','u','r','s')

/** @This extends @ref uprobe_event with specific burst events. */
enum uprobe_burst_event {
    UPROBE_BURST_SENTINEL = UPROBE_LOCAL,

    /** burst pipe blocked state change (bool) */
    UPROBE_BURST_UPDATE,
};

/** @This converts @ref uprobe_burst_event to a string.
 *
 * @param event event to convert
 * @return a string of NULL if invalid
 */
static inline const char *upipe_burst_event_str(int event)
{
    switch ((enum uprobe_burst_event)event) {
    UBASE_CASE_TO_STR(UPROBE_BURST_UPDATE);
    case UPROBE_BURST_SENTINEL: break;
    }
    return NULL;
}

/** @This allocates a burst pipe manager. */
struct upipe_mgr *upipe_burst_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_BURST_H_ */

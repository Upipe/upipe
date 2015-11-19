/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#ifndef _UPIPE_MODULS_UPIPE_STREAM_SWITCHER_H_
# define _UPIPE_MODULS_UPIPE_STREAM_SWITCHER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_STREAM_SWITCHER_SIGNATURE UBASE_FOURCC('s','t','s','w')
#define UPIPE_STREAM_SWITCHER_SUB_SIGNATURE UBASE_FOURCC('s','t','s','s')

/** @This extends uprobe_event with specific events for stream switcher
 * input. */
enum uprobe_stream_switcher_sub_event {
    UPROBE_STREAM_SWITCHER_SUB_SENTINEL = UPROBE_LOCAL,

    /** stream is sync */
    UPROBE_STREAM_SWITCHER_SUB_SYNC,
    /** stream is switched on */
    UPROBE_STREAM_SWITCHER_SUB_ENTERING,
    /** stream is switched off */
    UPROBE_STREAM_SWITCHER_SUB_LEAVING,
    /** stream is now destroyed */
    UPROBE_STREAM_SWITCHER_SUB_DESTROY,
};

static inline const char *uprobe_stream_switcher_sub_event_str(int event)
{
    switch ((enum uprobe_stream_switcher_sub_event)event) {
    UBASE_CASE_TO_STR(UPROBE_STREAM_SWITCHER_SUB_SYNC);
    UBASE_CASE_TO_STR(UPROBE_STREAM_SWITCHER_SUB_ENTERING);
    UBASE_CASE_TO_STR(UPROBE_STREAM_SWITCHER_SUB_LEAVING);
    UBASE_CASE_TO_STR(UPROBE_STREAM_SWITCHER_SUB_DESTROY);
    case UPROBE_STREAM_SWITCHER_SUB_SENTINEL: break;
    }
    return NULL;
}

/** @This returns the management structure for all stream switchers.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_stream_switcher_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe sync module - synchronize streams for muxing
 */

#ifndef _UPIPE_MODULES_UPIPE_SYNC_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SYNC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_SYNC_SIGNATURE UBASE_FOURCC('s', 'y', 'n', 'c')
#define UPIPE_SYNC_SUB_SIGNATURE UBASE_FOURCC('s', 'y', 'n', 's')

/** @This extends uprobe_event with specific events for sync. */
enum uprobe_sync_event {
    UPROBE_SYNC_SENTINEL = UPROBE_LOCAL,

    /** received picture event (unsigned int got_picture) */
    UPROBE_SYNC_PICTURE
};

/** @This returns the management structure for sync pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sync_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

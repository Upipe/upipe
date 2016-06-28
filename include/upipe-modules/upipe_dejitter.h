/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module calling dejtter on timestamps
 *
 * This module is used in conjunction with @ref upipe_nodemux. It is supposed
 * to be inserted in the pipeline after the DTS/PTS prog have been calculated,
 * for instance after the framer (upipe_nodemux on the contrary should be
 * before the framer). It considers each frame as a clock reference, and
 * throws events to fix the sys timestamps, normally caught by
 * @ref uprobe_dejitter.
 */

#ifndef _UPIPE_MODULES_UPIPE_DEJITTER_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DEJITTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_DEJITTER_SIGNATURE UBASE_FOURCC('d','j','t','r')

/** @This returns the management structure for all dejitter pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dejitter_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
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
 * @short Upipe NIC PTP implementation of uclock
 */

#ifndef _UPIPE_UCLOCK_PTP_H_
/** @hidden */
#define _UPIPE_UCLOCK_PTP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uclock.h>
#include <upipe/uprobe.h>

/** @This allocates a new uclock structure.
 *
 * @param uprobe probe catching log events for error reporting
 * @param interface NIC names, or NULL
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_ptp_alloc(struct uprobe *uprobe, const char *interface[2]);

#ifdef __cplusplus
}
#endif
#endif

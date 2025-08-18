/*
 * Pad picture urefs with blank space.
 *
 * Copyright (C) 2020 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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
 * @short Upipe module to pad picture urefs with blank space.
 */

#ifndef _UPIPE_MODULES_UPIPE_PAD_H_
#define _UPIPE_MODULES_UPIPE_PAD_H_

#include <upipe/ubase.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @This is the signature of a pad pipe. */
#define UPIPE_PAD_SIGNATURE    UBASE_FOURCC('p','a','d',' ')

/** @This returns the pad pipe manager.
 *
 * @return a pointer to the pad pipe manager
 */
struct upipe_mgr *upipe_pad_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_PAD_H_ */

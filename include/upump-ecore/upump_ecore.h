/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Cedric Bail <cedric.bail@free.fr>
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

/** &file
 * @short declarations for a Upipe main loop using Ecore
 */

#ifndef _UPUMP_ECORE_UPUMP_ECORE_H_
/** @hidden */
#define _UPUMP_ECORE_UPUMP_ECORE_H_

#include <upipe/upump.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPUMP_ECORE_SIGNATURE UBASE_FOURCC('e','c','o','r')

/** @This allocates and initializes a upump_ecore_mgr structure.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ecore_mgr_alloc(uint16_t upump_pool_depth,
                                        uint16_t upump_blocker_pool_depth);

#ifdef __cplusplus
}
#endif
#endif

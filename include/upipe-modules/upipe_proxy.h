/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module - acts as a proxy to another module
 * This is particularly helpful for split pipe, where you would need a proxy
 * as an input pipe, to detect end of streams.
 */

#ifndef _UPIPE_MODULES_UPIPE_PROXY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PROXY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_PROXY_SIGNATURE UBASE_FOURCC('p','r','x','y')

/** @This defines a function called when the proxy is released. */
typedef void (*upipe_proxy_released)(struct upipe *);

/** @This returns the management structure for proxy pipes. Please note that
 * the refcount for super_mgr is not incremented, so super_mgr belongs to the
 * callee.
 *
 * @param super_mgr management structures for pipes we're proxying for
 * @param proxy_released function called when a proxy pipe is released
 * @return pointer to manager
 */
struct upipe_mgr *upipe_proxy_mgr_alloc(struct upipe_mgr *super_mgr,
                                        upipe_proxy_released proxy_released);

/** @This returns the superpipe manager.
 *
 * @param mgr proxy_mgr structure
 * @return pointer to superpipe manager
 */
struct upipe_mgr *upipe_proxy_mgr_get_super_mgr(struct upipe_mgr *mgr);

#ifdef __cplusplus
}
#endif
#endif

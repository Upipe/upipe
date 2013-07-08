/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe standard uref manager
 */

#ifndef _UPIPE_UREF_STD_H_
/** @hidden */
#define _UPIPE_UREF_STD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>

/** @This allocates a new instance of the standard uref manager
 *
 * @param uref_pool_depth maximum number of uref structures in the pool
 * @param udict_mgr udict manager to use to allocate udict structures
 * @param control_attr_size extra attributes space for control packets
 * @return pointer to manager, or NULL in case of error
 */
struct uref_mgr *uref_std_mgr_alloc(uint16_t uref_pool_depth,
                                    struct udict_mgr *udict_mgr,
                                    int control_attr_size);

#ifdef __cplusplus
}
#endif
#endif

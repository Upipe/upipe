/*****************************************************************************
 * uref_std.h: declarations for the standard uref manager
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UREF_STD_H_
/** @hidden */
#define _UPIPE_UREF_STD_H_

#include <upipe/uref.h>

/*
 * Please note that you must maintain at least one manager per thread,
 * because due to the pool implementation, only one thread can make
 * allocations (structures can be released from any thread though).
 */

/** @This allocates a new instance of the standard uref manager
 *
 * @param pool_depth maximum number of uref structures in the pool
 * @param attr_size default attributes structure size (if set to -1, a default
 * sensible value is used)
 * @param control_attr_size extra attributes space for control packets; also
 * limit from which uref_t structures are not recycled in the pool, but
 * directly allocated and freed (if set to -1, a default sensible value is used)
 * @return pointer to manager, or NULL in case of error
 */
struct uref_mgr *uref_std_mgr_alloc(unsigned int pool_depth,
                                    int attr_size, int control_attr_size);

#endif

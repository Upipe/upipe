/*
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
 */

/** @file
 * @short Upipe trivial memory allocator
 * This memory allocator directly calls malloc() and free(), without trying to
 * organize data pools.
 */

#ifndef _UPIPE_UMEM_ALLOC_H_
/** @hidden */
#define _UPIPE_UMEM_ALLOC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/umem.h>

/** @This allocates a new instance of the umem alloc manager allocating buffers
 * from application memory directly with malloc()/free(), without any pool.
 *
 * @return pointer to manager, or NULL in case of error
 */
struct umem_mgr *umem_alloc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

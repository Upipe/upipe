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
 * @short Upipe pool-based memory allocator
 * This memory allocator keeps released memory blocks in pools organized by
 * power of 2's sizes, and reverts to malloc() and free() if the pool
 * underflows or overflows.
 */

#ifndef _UPIPE_UMEM_POOL_H_
/** @hidden */
#define _UPIPE_UMEM_POOL_H_

#include <upipe/umem.h>

/** @This allocates a new instance of the umem pool manager allocating buffers
 * from application memory, using pools in power of 2's.
 *
 * @param pool0_size size (in octets) of the smallest allocatable buffer; it
 * must be a power of 2
 * @param nb_pools number of buffer pools to maintain, with sizes in power of
 * 2's increments, followed, for each pool, by the maximum number of buffers
 * to keep in the pool (unsigned int); larger buffers will be directly managed
 * with malloc() and free()
 * @return pointer to manager, or NULL in case of error
 */
struct umem_mgr *umem_pool_mgr_alloc(size_t pool0_size, size_t nb_pools, ...);


/** @This allocates a new instance of the umem pool manager allocating buffers
 * from application memory, using pools in power of 2's, with a simpler API.
 *
 * @param base_pools_depth number of buffers to keep in the pool for the smaller
 * buffers; for larger buffers the same number is used, divided by 2, 4, or 8
 * @return pointer to manager, or NULL in case of error
 */
struct umem_mgr *umem_pool_mgr_alloc_simple(uint16_t base_pools_depth);

#endif

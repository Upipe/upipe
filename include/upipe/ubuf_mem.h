/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe functions to allocate ubuf managers using umem storage
 */

#ifndef _UPIPE_UBUF_MEM_H_
/** @hidden */
#define _UPIPE_UBUF_MEM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>

#include <stdint.h>

/** @hidden */
struct ubuf_mgr;
/** @hidden */
struct umem_mgr;
/** @hidden */
struct uref;

/** @This allocates an ubuf manager using umem from a flow definition.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param flow_def flow definition packet
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_mem_mgr_alloc_from_flow_def(uint16_t ubuf_pool_depth,
        uint16_t shared_pool_depth, struct umem_mgr *umem_mgr,
        struct uref *flow_def);

#ifdef __cplusplus
}
#endif
#endif

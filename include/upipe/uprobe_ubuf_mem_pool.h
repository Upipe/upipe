/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short probe catching provide_request events asking for a ubuf manager, and keeping the managers in a pool
 */

#ifndef _UPIPE_UPROBE_UBUF_MEM_POOL_H_
/** @hidden */
#define _UPIPE_UPROBE_UBUF_MEM_POOL_H_

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uatomic.h>

/** @hidden */
struct umem_mgr;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_ubuf_mem_pool {
    /** pointer to umem_mgr to use to allocate ubuf manager */
    struct umem_mgr *umem_mgr;
    /** depth of the ubuf pool */
    uint16_t ubuf_pool_depth;
    /** depth of the shared object pool */
    uint16_t shared_pool_depth;

    /** chained list of ubuf managers, elements are never removed */
    uatomic_ptr_t first;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_ubuf_mem_pool, uprobe)

/** @This initializes an already allocated uprobe_ubuf_mem_pool structure.
 *
 * @param uprobe_ubuf_mem_pool pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param umem_mgr memory allocator to use for buffers
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *
    uprobe_ubuf_mem_pool_init(struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool,
                              struct uprobe *next, struct umem_mgr *umem_mgr,
                              uint16_t ubuf_pool_depth,
                              uint16_t shared_pool_depth);

/** @This instructs an existing probe to release all manager currently
 * kept in pools. Please not that this function is not thread-safe, and mustn't
 * be used if the probe may be called from another thread.
 *
 * @param uprobe_ubuf_mem_pool structure to vacuum
 */
void uprobe_ubuf_mem_pool_vacuum(struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool);

/** @This cleans a uprobe_ubuf_mem_pool structure.
 *
 * @param uprobe_ubuf_mem_pool structure to clean
 */
void uprobe_ubuf_mem_pool_clean(struct uprobe_ubuf_mem_pool *uprobe_ubuf_mem_pool);

/** @This allocates a new uprobe_ubuf_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param umem_mgr memory allocator to use for buffers
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ubuf_mem_pool_alloc(struct uprobe *next,
                                          struct umem_mgr *umem_mgr,
                                          uint16_t ubuf_pool_depth,
                                          uint16_t shared_pool_depth);

/** @This changes the umem_mgr used by this probe.
 *
 * @param uprobe pointer to probe
 * @param umem_mgr new umem manager to use
 */
void uprobe_ubuf_mem_pool_set(struct uprobe *uprobe, struct umem_mgr *umem_mgr);

#endif

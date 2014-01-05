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
 * @short probe catching new_flow_format events and providing a given ubuf manager using umem storage
 */

#ifndef _UPIPE_UPROBE_UBUF_MEM_H_
/** @hidden */
#define _UPIPE_UPROBE_UBUF_MEM_H_

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

/** @hidden */
struct umem_mgr;

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_ubuf_mem {
    /** pointer to umem_mgr to use to allocate ubuf manager */
    struct umem_mgr *umem_mgr;
    /** depth of the ubuf pool */
    uint16_t ubuf_pool_depth;
    /** depth of the shared object pool */
    uint16_t shared_pool_depth;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_ubuf_mem, uprobe)

/** @This initializes an already allocated uprobe_ubuf_mem structure.
 *
 * @param uprobe_ubuf_mem pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param umem_mgr memory allocator to use for buffers
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ubuf_mem_init(struct uprobe_ubuf_mem *uprobe_ubuf_mem,
                                    struct uprobe *next,
                                    struct umem_mgr *umem_mgr,
                                    uint16_t ubuf_pool_depth,
                                    uint16_t shared_pool_depth);

/** @This cleans a uprobe_ubuf_mem structure.
 *
 * @param uprobe_ubuf_mem structure to clean
 */
void uprobe_ubuf_mem_clean(struct uprobe_ubuf_mem *uprobe_ubuf_mem);

/** @This allocates a new uprobe_ubuf_mgr structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param umem_mgr memory allocator to use for buffers
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param shared_pool_depth maximum number of shared structures in the pool
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_ubuf_mem_alloc(struct uprobe *next,
                                     struct umem_mgr *umem_mgr,
                                     uint16_t ubuf_pool_depth,
                                     uint16_t shared_pool_depth);

/** @This changes the umem_mgr used by this probe.
 *
 * @param uprobe pointer to probe
 * @param umem_mgr new umem manager to use
 */
void uprobe_ubuf_mem_set(struct uprobe *uprobe, struct umem_mgr *umem_mgr);

#endif

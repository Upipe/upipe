/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
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

#include "upipe/ubase.h"

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

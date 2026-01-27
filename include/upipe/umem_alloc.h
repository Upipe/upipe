/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * SPDX-License-Identifier: MIT
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

#include "upipe/umem.h"

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

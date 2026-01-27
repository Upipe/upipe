/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe inline manager of dictionary of attributes
 * This manager stores all attributes inline inside a single umem block.
 * This is designed in order to minimize calls to memory allocators, and
 * to transmit dictionaries over streams.
 */

#ifndef _UPIPE_UDICT_INLINE_H_
/** @hidden */
#define _UPIPE_UDICT_INLINE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/udict.h"

struct umem_mgr;

/** @This allocates a new instance of the inline udict manager.
 *
 * @param udict_pool_depth maximum number of udict structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param min_size minimum allocated space for the udict (if set to -1, a
 * default sensible value is used)
 * @param extra_size extra space added when the udict needs to be resized
 * (if set to -1, a default sensible value is used)
 * @return pointer to manager, or NULL in case of error
 */
struct udict_mgr *udict_inline_mgr_alloc(uint16_t udict_pool_depth,
                                         struct umem_mgr *umem_mgr,
                                         int min_size, int extra_size);

#ifdef __cplusplus
}
#endif
#endif

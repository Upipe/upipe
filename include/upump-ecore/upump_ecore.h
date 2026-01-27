/*
 * Copyright (C) 2014-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Cedric Bail <cedric.bail@free.fr>
 *
 * SPDX-License-Identifier: MIT
 */

/** &file
 * @short declarations for a Upipe main loop using Ecore
 */

#ifndef _UPUMP_ECORE_UPUMP_ECORE_H_
/** @hidden */
#define _UPUMP_ECORE_UPUMP_ECORE_H_

#include "upipe/upump.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UPUMP_ECORE_SIGNATURE UBASE_FOURCC('e','c','o','r')

/** @This allocates and initializes a upump_ecore_mgr structure.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ecore_mgr_alloc(uint16_t upump_pool_depth,
                                        uint16_t upump_blocker_pool_depth);

#ifdef __cplusplus
}
#endif
#endif

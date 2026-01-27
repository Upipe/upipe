/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** &file
 * @short declarations for a Upipe main loop using libev
 */

#ifndef _UPUMP_EV_UPUMP_EV_H_
/** @hidden */
#define _UPUMP_EV_UPUMP_EV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <ev.h>

#include "upipe/upump.h"

#define UPUMP_EV_SIGNATURE UBASE_FOURCC('e','v',' ',' ')

/** @This allocates and initializes a upump_mgr structure bound to a given
 * ev loop.
 *
 * @param ev_loop pointer to an ev loop
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc(struct ev_loop *ev_loop,
                                     uint16_t upump_pool_depth,
                                     uint16_t upump_blocker_pool_depth);

/** @This allocates and initializes a upump_mgr structure bound to the
 * default ev loop.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc_default(uint16_t upump_pool_depth,
                                             uint16_t upump_blocker_pool_depth);

/** @This allocates and initializes a upump_mgr structure bound to an
 * allocated ev loop.
 *
 * @param upump_pool_depth maximum number of upump structures in the pool
 * @param upump_blocker_pool_depth maximum number of upump_blocker structures in
 * the pool
 * @return pointer to the wrapped upump_mgr structure
 */
struct upump_mgr *upump_ev_mgr_alloc_loop(uint16_t upump_pool_depth,
                                          uint16_t upump_blocker_pool_depth);

#ifdef __cplusplus
}
#endif
#endif

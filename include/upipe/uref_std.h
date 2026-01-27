/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe standard uref manager
 */

#ifndef _UPIPE_UREF_STD_H_
/** @hidden */
#define _UPIPE_UREF_STD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"

/** @This allocates a new instance of the standard uref manager
 *
 * @param uref_pool_depth maximum number of uref structures in the pool
 * @param udict_mgr udict manager to use to allocate udict structures
 * @param control_attr_size extra attributes space for control packets
 * @return pointer to manager, or NULL in case of error
 */
struct uref_mgr *uref_std_mgr_alloc(uint16_t uref_pool_depth,
                                    struct udict_mgr *udict_mgr,
                                    int control_attr_size);

#ifdef __cplusplus
}
#endif
#endif

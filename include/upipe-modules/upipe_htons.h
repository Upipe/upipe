/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module - htons
 */

#ifndef _UPIPE_MODULES_UPIPE_HTONS_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HTONS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/upipe.h"

#define UPIPE_HTONS_SIGNATURE UBASE_FOURCC('h','t','o','n')

/** @This returns the management structure for skip pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_htons_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

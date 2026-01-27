/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to aggregate complete packets up to specified MTU
 */

#ifndef _UPIPE_MODULES_UPIPE_AGGREGATE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_AGGREGATE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_AGG_SIGNATURE UBASE_FOURCC('a','g','g','g')

/** @This returns the management structure for all agg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_agg_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

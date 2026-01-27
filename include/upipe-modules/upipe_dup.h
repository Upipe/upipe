/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module allowing to duplicate to several outputs
 */

#ifndef _UPIPE_MODULES_UPIPE_DUP_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DUP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_DUP_SIGNATURE UBASE_FOURCC('d','u','p',' ')
#define UPIPE_DUP_OUTPUT_SIGNATURE UBASE_FOURCC('d','u','p','o')

/** @This returns the management structure for all dup pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dup_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

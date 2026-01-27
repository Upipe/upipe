/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module converting sound and pic ubuf to block
 */

#ifndef _UPIPE_MODULES_UPIPE_CONVERT_TO_BLOCK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_CONVERT_TO_BLOCK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TBLK_SIGNATURE UBASE_FOURCC('t','b','l','k')

/** @This returns the management structure for tblk pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_tblk_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

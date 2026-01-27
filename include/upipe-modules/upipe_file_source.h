/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe source module for files
 */

#ifndef _UPIPE_MODULES_UPIPE_FILE_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_FILE_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_FSRC_SIGNATURE UBASE_FOURCC('f','s','r','c')

/** @This returns the management structure for all file sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

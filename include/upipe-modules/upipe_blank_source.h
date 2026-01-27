/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe source module generating a black/blank signal
 */

#ifndef _UPIPE_MODULES_UPIPE_BLANK_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_BLANK_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_BLKSRC_SIGNATURE UBASE_FOURCC('b','l','k','s')

/** @This returns the management structure for all file sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_blksrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

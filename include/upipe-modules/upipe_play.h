/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module synchronizing latencies of flows belonging to a program
 */

#ifndef _UPIPE_MODULES_UPIPE_PLAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PLAY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_PLAY_SIGNATURE UBASE_FOURCC('p','l','a','y')
#define UPIPE_PLAY_SUB_SIGNATURE UBASE_FOURCC('p','l','a','s')

/** @This returns the management structure for all play pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_play_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

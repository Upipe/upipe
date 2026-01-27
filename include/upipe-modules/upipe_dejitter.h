/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module calling dejtter on timestamps
 *
 * The input of the super-pipe is supposed to be the clock ref. Its output
 * and the outputs of the subpipes are then dejittered with the clock ts probe.
 */

#ifndef _UPIPE_MODULES_UPIPE_DEJITTER_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DEJITTER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_DEJITTER_SIGNATURE UBASE_FOURCC('d','j','t','r')
#define UPIPE_DEJITTER_SUB_SIGNATURE UBASE_FOURCC('d','j','t','s')

/** @This returns the management structure for all dejitter pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dejitter_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

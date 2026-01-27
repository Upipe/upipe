/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module discarding input uref when the output pipe is blocking
 */

#ifndef _UPIPE_MODULES_UPIPE_DISCARD_BLOCKING_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DISCARD_BLOCKING_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"

#define UPIPE_DISBLO_SIGNATURE UBASE_FOURCC('d','i','s','b')

/** @This returns the management structure for discard blocking pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_disblo_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module trimming dead frames off a video stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_VIDEO_TRIM_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_VIDEO_TRIM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_VTRIM_SIGNATURE UBASE_FOURCC('v','t','r','m')

/** @This returns the management structure for all vtrim pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_vtrim_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe swrcale (ffmpeg) module
 */

#ifndef _UPIPE_SWRESAMPLE_UPIPE_SWR_H_
/** @hidden */
#define _UPIPE_SWRESAMPLE_UPIPE_SWR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SWR_SIGNATURE UBASE_FOURCC('s','w','r',' ')

/** @This returns the management structure for swr pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_swr_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

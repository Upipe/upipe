/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe linear module sending output identical to input
 */

#ifndef _UPIPE_DVEO_UPIPE_DVEO_ASI_SOURCE_H_
/** @hidden */
#define _UPIPE_DVEO_UPIPE_DVEO_ASI_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/upipe.h"

#define UPIPE_DVEO_ASI_SRC_SIGNATURE UBASE_FOURCC('d','v','a','s')

/** @This returns the management structure for dveo_asi_src pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dveo_asi_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

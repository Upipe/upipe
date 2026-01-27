/*
 * 10 bit packing
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe pack10bit module
 */

#ifndef _UPIPE_HBRMT_UPIPE_PACK10BIT_H_
/** @hidden */
#define _UPIPE_HBRMT_UPIPE_PACK10BIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_PACK10BIT_SIGNATURE UBASE_FOURCC('p','1','0','b')

#include "upipe/upipe.h"

/** @This returns the management structure for pack10bit pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_pack10bit_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2013-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe ebur128
 */

#ifndef _UPIPE_EBUR128_UPIPE_EBUR128_H_
/** @hidden */
#define _UPIPE_EBUR128_UPIPE_EBUR128_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref_attr.h"
#include <stdint.h>

UREF_ATTR_FLOAT(ebur128, momentary, "ebur128.momentary", momentary loudness)
UREF_ATTR_FLOAT(ebur128, lra, "ebur128.lra", loudness range)
UREF_ATTR_FLOAT(ebur128, global, "ebur128.global", global integrated loudness)

#define UPIPE_EBUR128_SIGNATURE UBASE_FOURCC('r', '1', '2', '8')

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ebur128_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

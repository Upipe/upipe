/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module decapsulating DVB TTML subtitle segments from PES
 * payloads (EN 303 560 5.2.2.2)
 */

#ifndef _UPIPE_TS_UPIPE_TS_TTML_DECAPS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_TTML_DECAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_TTMLD_SIGNATURE UBASE_FOURCC('t','s','t','t')

/** @This returns the management structure for all ts_ttmld pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_ttmld_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of a DVB TTML subtitle
 * stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_TTML_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_TTML_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TTMLF_SIGNATURE UBASE_FOURCC('t','t','m','f')
/** We only accept the EN 303 560 PES data fields. */
#define UPIPE_TTMLF_EXPECTED_FLOW_DEF "block.dvb_ttml_subtitle."

/** @This returns the management structure for all ttmlf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ttmlf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

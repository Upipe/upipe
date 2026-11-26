/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe attributes for DVB TTML subtitle segments
 */

#ifndef _UPIPE_TS_UREF_TS_TTML_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_TTML_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

/** @This is the flow definition of a stream of TTML documents. */
#define UREF_TS_TTML_FLOW_DEF "block.ttml.pic.sub."

/* The media time of the segment is the instant of the TTML timeline
 * that the PTS of its PES packet corresponds to (EN 303 560 5.2.4.1). */
UREF_ATTR_UNSIGNED(ts_ttml, mediatime, "t.ttml.mediatime",
        segment media time in UCLOCK_FREQ units)

#ifdef __cplusplus
}
#endif
#endif

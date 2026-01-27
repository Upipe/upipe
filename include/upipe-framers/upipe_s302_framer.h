/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of a SMPTE 302 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_S302_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_S302_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_S302F_SIGNATURE UBASE_FOURCC('s','3','2','f')
/** We only accept the EN 300 706 elementary stream. */
#define UPIPE_S302F_EXPECTED_FLOW_DEF "block.s302m.sound."

/** @This returns the management structure for all telxf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s302f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

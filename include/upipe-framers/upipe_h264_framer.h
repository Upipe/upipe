/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of an ISO 14496-10 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_H264_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_H264_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_H264F_SIGNATURE UBASE_FOURCC('2','6','4','f')
/** We only accept the ISO 14496-10 annex B elementary stream. */
#define UPIPE_H264F_EXPECTED_FLOW_DEF "block.h264."

/** @This returns the management structure for all h264f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_h264f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

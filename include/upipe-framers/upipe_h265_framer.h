/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of an ITU-T H.265 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_H265_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_H265_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_H265F_SIGNATURE UBASE_FOURCC('h','e','v','f')
/** We only accept the ISO 14496-10 annex B elementary stream. */
#define UPIPE_H265F_EXPECTED_FLOW_DEF "block.hevc."

/** @This returns the management structure for all h265f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_h265f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

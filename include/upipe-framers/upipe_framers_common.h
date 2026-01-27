/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe common utils for framers
 */

#ifndef _UPIPE_FRAMERS_UPIPE_FRAMERS_COMMON_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_FRAMERS_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubuf_block_stream.h"

/** @This scans for an MPEG-style 3-octet start code in a linear buffer.
 *
 * @param p linear buffer
 * @param end end of linear buffer
 * @param state state of the algorithm
 * @return pointer to start code, or end if not found
 */
const uint8_t *upipe_framers_mpeg_scan(const uint8_t *restrict p,
                                       const uint8_t *end,
                                       uint32_t *restrict state);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2014 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from an Opus stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_OPUS_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_OPUS_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_OPUSF_SIGNATURE UBASE_FOURCC('o','p','u','f')

/** @This returns the management structure for all opusf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_opusf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

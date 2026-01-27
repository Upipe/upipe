/*
 * Copyright (C) 2019 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module performing SCTE-35 to SCTE-104 conversion
 */

#ifndef _UPIPE_TS_UPIPE_TS_SCTE104_GENERATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SCTE104_GENERATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_SCTE104_GENERATOR_SIGNATURE UBASE_FOURCC('t','s','c','1')

/** @This returns the management structure for s337e pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte104_generator_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

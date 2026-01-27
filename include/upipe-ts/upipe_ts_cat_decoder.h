/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the conditional access table
 */

#ifndef _UPIPE_TS_UPIPE_TS_CAT_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_CAT_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_CATD_SIGNATURE UBASE_FOURCC('t','s',0x01,'d')

/** @This returns the management structure for all ts_catd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_catd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

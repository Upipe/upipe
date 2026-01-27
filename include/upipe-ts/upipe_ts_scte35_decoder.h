/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the splice information table of SCTE-35 streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_SCTE35_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SCTE35_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_SCTE35D_SIGNATURE UBASE_FOURCC('t','s',0xfc,'d')

/** @This returns the management structure for all ts_scte35d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35d_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

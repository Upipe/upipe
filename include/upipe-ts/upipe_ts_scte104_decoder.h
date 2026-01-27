/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the operation tables of SCTE 104 streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_SCTE104_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SCTE104_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_SCTE104D_SIGNATURE UBASE_FOURCC('s','1','0','4')
#define UPIPE_TS_SCTE104D_OUTPUT_SIGNATURE UBASE_FOURCC('S','1','0','4')

/** @This returns the management structure for all ts_scte104d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte104d_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

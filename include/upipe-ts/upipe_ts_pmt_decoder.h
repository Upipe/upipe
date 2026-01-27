/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the program map table of TS streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_PMT_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PMT_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_PMTD_SIGNATURE UBASE_FOURCC('t','s','2','d')

/** @This returns the management structure for all ts_pmtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pmtd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

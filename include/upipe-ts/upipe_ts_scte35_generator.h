/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module generating SCTE-35 Splice Information Table
 */

#ifndef _UPIPE_TS_UPIPE_TS_SCTE35_GENERATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_SCTE35_GENERATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_mux.h"

#define UPIPE_TS_SCTE35G_SIGNATURE UBASE_FOURCC('t','s',0xfc,'g')

/** @This returns the management structure for all ts_scte35g pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_scte35g_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

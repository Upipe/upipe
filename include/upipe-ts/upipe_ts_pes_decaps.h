/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decapsulating (removing) PES header of TS packets
 */

#ifndef _UPIPE_TS_UPIPE_TS_PES_DECAPS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PES_DECAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_PESD_SIGNATURE UBASE_FOURCC('t','s','p','d')

/** @This returns the management structure for all ts_pesd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pesd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

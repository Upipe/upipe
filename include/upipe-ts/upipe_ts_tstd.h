/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module tstding that a buffer contains a given number of
 * aligned TS packets
 */

#ifndef _UPIPE_TS_UPIPE_TS_TSTD_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_TSTD_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_TSTD_SIGNATURE UBASE_FOURCC('t','s','t','d')

/** @This returns the management structure for all ts_tstd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_tstd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

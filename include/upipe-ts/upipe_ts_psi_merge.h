/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module merging PSI sections from TS input
 */

#ifndef _UPIPE_TS_UPIPE_TS_PSI_MERGE_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PSI_MERGE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_PSIM_SIGNATURE UBASE_FOURCC('t','s','p','m')

/** @This returns the management structure for all ts_psim pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psim_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

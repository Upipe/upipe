/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module splitting tables of the PSI of a transport stream
 */

#ifndef _UPIPE_TS_UPIPE_TS_PSI_SPLIT_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PSI_SPLIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_PSI_SPLIT_SIGNATURE UBASE_FOURCC('t','s','p','Y')
#define UPIPE_TS_PSI_SPLIT_OUTPUT_SIGNATURE UBASE_FOURCC('t','s','p','Z')

/** @This returns the management structure for all ts_psi_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psi_split_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

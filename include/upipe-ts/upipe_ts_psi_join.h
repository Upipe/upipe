/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module joining tables of the PSI of a transport stream
 */

#ifndef _UPIPE_TS_UPIPE_TS_PSI_JOIN_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PSI_JOIN_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_PSI_JOIN_SIGNATURE UBASE_FOURCC('t','s','p','A')
#define UPIPE_TS_PSI_JOIN_INPUT_SIGNATURE UBASE_FOURCC('t','s','p','B')

/** @This returns the management structure for all ts_psi_join pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psi_join_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

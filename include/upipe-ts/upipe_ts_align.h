/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module outputting one aligned TS packet per uref
 */

#ifndef _UPIPE_TS_UPIPE_TS_ALIGN_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_ALIGN_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_ALIGN_SIGNATURE UBASE_FOURCC('t','s','a','l')

/** @This returns the management structure for all ts_align pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_align_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

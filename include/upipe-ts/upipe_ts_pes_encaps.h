/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module encapsulating access units into PES packets
 */

#ifndef _UPIPE_TS_UPIPE_TS_PES_ENCAPS_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_PES_ENCAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_PESE_SIGNATURE UBASE_FOURCC('t','s','p','e')

/** @This returns the management structure for all ts_pese pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pese_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

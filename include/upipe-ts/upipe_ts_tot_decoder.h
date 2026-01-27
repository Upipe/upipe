/*
 * Copyright (C) 2023 EasyTools S.A.S.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the time offset table of DVB streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_TOT_DECODER_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_TOT_DECODER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_TOTD_SIGNATURE UBASE_FOURCC('t','s',0x73,'d')

/** @This returns the management structure for all ts_totd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_totd_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

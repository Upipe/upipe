/*
 * Copyright (C) 2024 EasyTools S.A.S.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module generating the application information table of DVB
 * streams
 */

#ifndef _UPIPE_TS_UPIPE_TS_AIT_GENERATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_AIT_GENERATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_demux.h"

#define UPIPE_TS_AITG_SIGNATURE UBASE_FOURCC('t','s',0x74,'g')

/** @This returns the management structure for all ts_aitg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_aitg_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

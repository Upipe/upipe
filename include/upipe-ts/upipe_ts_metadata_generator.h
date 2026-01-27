/*
 * Copyright (C) 2023 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module generating metadata
 */

#ifndef _UPIPE_TS_UPIPE_TS_METADATA_GENERATOR_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_METADATA_GENERATOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe-ts/upipe_ts_mux.h"

#define UPIPE_TS_MDG_SIGNATURE UBASE_FOURCC('t','s','M','g')

/** @This returns the management structure for all ts_mdg pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_mdg_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

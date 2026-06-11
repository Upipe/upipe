/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to force scan
 */

#ifndef _UPIPE_MODULES_UPIPE_SETSCAN_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SETSCAN_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SETSCAN_SIGNATURE UBASE_FOURCC('s','s','c','n')

/** @This returns the management structure for all setscan pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setscan_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

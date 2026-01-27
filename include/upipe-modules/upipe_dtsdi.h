/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd.
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_DTSDI_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DTSDI_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_DTSDI_SIGNATURE UBASE_FOURCC('d', 't', 's', 'd')

struct upipe_mgr *upipe_dtsdi_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe source module generating a sine wave
 * This module is particularly helpful to test sound sinks.
 */

#ifndef _UPIPE_MODULES_UPIPE_SINE_WAVE_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SINE_WAVE_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SINESRC_SIGNATURE UBASE_FOURCC('s','i','n','s')

/** @This returns the management structure for all file sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sinesrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

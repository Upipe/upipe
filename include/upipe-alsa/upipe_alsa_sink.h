/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe sink module for alsa sound system
 */

#ifndef _UPIPE_ALSA_UPIPE_ALSA_SINK_H_
/** @hidden */
#define _UPIPE_ALSA_UPIPE_ALSA_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_ALSINK_SIGNATURE UBASE_FOURCC('a', 'l', 's', 's')

/** @This returns the management structure for all alsa sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_alsink_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

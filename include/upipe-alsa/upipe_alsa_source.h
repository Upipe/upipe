/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd.
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe source module for alsa sound system
 */

#ifndef _UPIPE_ALSA_UPIPE_ALSA_SOURCE_H_
/** @hidden */
#define _UPIPE_ALSA_UPIPE_ALSA_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_ALSOURCE_SIGNATURE UBASE_FOURCC('a', 'l', 's', 'o')

/** @This returns the management structure for all alsa sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_alsource_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

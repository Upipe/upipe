/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to output fixed size sound buffers.
 */

#ifndef _UPIPE_MODULES_UPIPE_AUDIO_COPY_H_
#define _UPIPE_MODULES_UPIPE_AUDIO_COPY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"

/** @hidden */
struct upipe_mgr;

/** @This is the audio frame pipe signature. */
#define UPIPE_AUDIO_COPY_SIGNATURE UBASE_FOURCC('a','c','p','y')

/** @This returns the audio frame pipe management structure. */
struct upipe_mgr *upipe_audio_copy_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module splitting packed audio to several planar or packed outputs
 */

#ifndef _UPIPE_MODULES_UPIPE_AUDIO_SPLIT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_AUDIO_SPLIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref_attr.h"

#define UPIPE_AUDIO_SPLIT_SIGNATURE UBASE_FOURCC('a','s','p','l')
#define UPIPE_AUDIO_SPLIT_OUTPUT_SIGNATURE UBASE_FOURCC('a','s','p','o')

UREF_ATTR_UNSIGNED(audio_split, bitfield, "aspo.b", audio split bit field)

/** @This returns the management structure for all audio_split pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audio_split_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

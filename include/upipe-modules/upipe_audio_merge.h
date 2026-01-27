/*
 * Copyright (C) 2015 - 2019 Open Broadcast Systems Ltd.
 *
 * Authors: Kieran Kunhya
 *          Sam Willcocks
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module merging audio from several input subpipes into a single buffer
 *
 * This module accepts audio from input subpipes and outputs a single pipe of n-channel/plane audio.
 * Each input must be sent urefs at the same rate, as the pipe will not output until every input channel
 * has received a uref. The pipe makes no attempt to retime or resample.
 * Input audio must have the same format, save for the number of channels/planes.
 */

#ifndef _UPIPE_MODULES_UPIPE_AUDIO_MERGE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_AUDIO_MERGE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/upump.h"
#include "upipe/uref_attr.h"

#define UPIPE_AUDIO_MERGE_SIGNATURE UBASE_FOURCC('a','m','g','l')
#define UPIPE_AUDIO_MERGE_INPUT_SIGNATURE UBASE_FOURCC('a','m','g','i')

/** @This returns the management structure for all audio_merge pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audio_merge_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

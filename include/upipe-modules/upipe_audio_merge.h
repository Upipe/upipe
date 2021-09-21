/*
 * Copyright (C) 2015 - 2019 Open Broadcast Systems Ltd.
 *
 * Authors: Kieran Kunhya
 *          Sam Willcocks
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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

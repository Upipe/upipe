/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module synchronizing latencies of flows belonging to a program
 */

#ifndef _UPIPE_MODULES_UPIPE_PLAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_PLAY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_PLAY_SIGNATURE UBASE_FOURCC('p','l','a','y')
#define UPIPE_PLAY_SUB_SIGNATURE UBASE_FOURCC('p','l','a','s')

/** @This extends upipe_command with specific commands for play pipes. */
enum upipe_play_command {
    UPIPE_PLAY_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current output latency (uint64_t *) */
    UPIPE_PLAY_GET_OUTPUT_LATENCY,
    /** sets the output latency (uint64_t) */
    UPIPE_PLAY_SET_OUTPUT_LATENCY
};

/** @This returns the management structure for all play pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_play_mgr_alloc(void);

/** @This returns the current output latency;
 *
 * @param upipe description structure of the pipe
 * @param latency_p filled with the output latency
 * @return an error code
 */
static inline int upipe_play_get_output_latency(struct upipe *upipe,
                                                uint64_t *latency_p)
{
    return upipe_control(upipe, UPIPE_PLAY_GET_OUTPUT_LATENCY,
                         UPIPE_PLAY_SIGNATURE, latency_p);
}

/** @This sets the output latency;
 *
 * @param upipe description structure of the pipe
 * @param latency new output latency
 * @return an error code
 */
static inline int upipe_play_set_output_latency(struct upipe *upipe,
                                                uint64_t latency)
{
    return upipe_control(upipe, UPIPE_PLAY_SET_OUTPUT_LATENCY,
                         UPIPE_PLAY_SIGNATURE, latency);
}

#ifdef __cplusplus
}
#endif
#endif

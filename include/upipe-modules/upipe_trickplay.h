/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module facilitating trick play operations
 */

#ifndef _UPIPE_MODULES_UPIPE_TRICKPLAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_TRICKPLAY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TRICKP_SIGNATURE UBASE_FOURCC('t','r','c','k')
#define UPIPE_TRICKP_SUB_SIGNATURE UBASE_FOURCC('t','r','c','s')

/** @This extends upipe_command with specific commands for trickp pipes. */
enum upipe_trickp_command {
    UPIPE_TRICKP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current playing rate (struct urational *) */
    UPIPE_TRICKP_GET_RATE,
    /** sets the playing rate (struct urational) */
    UPIPE_TRICKP_SET_RATE
};

/** @This returns the management structure for all trickp pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_trickp_mgr_alloc(void);

/** @This returns the current playing rate.
 *
 * @param upipe description structure of the pipe
 * @param rate_p filled with the current rate
 * @return an error code
 */
static inline int upipe_trickp_get_rate(struct upipe *upipe,
                                        struct urational *rate_p)
{
    return upipe_control(upipe, UPIPE_TRICKP_GET_RATE,
                         UPIPE_TRICKP_SIGNATURE, rate_p);
}

/** @This sets the playing rate.
 *
 * @param upipe description structure of the pipe
 * @param rate new rate (1/1 = normal play, 0 = pause)
 * @return an error code
 */
static inline int upipe_trickp_set_rate(struct upipe *upipe,
                                        struct urational rate)
{
    return upipe_control(upipe, UPIPE_TRICKP_SET_RATE,
                         UPIPE_TRICKP_SIGNATURE, rate);
}

#ifdef __cplusplus
}
#endif
#endif

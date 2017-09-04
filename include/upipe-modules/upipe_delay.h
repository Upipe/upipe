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
 * @short Upipe module adding a delay to all dates
 */

#ifndef _UPIPE_MODULES_UPIPE_DELAY_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DELAY_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <upipe/uref.h>

#define UPIPE_DELAY_SIGNATURE UBASE_FOURCC('d','l','a','y')

/** @This extends upipe_command with specific commands for delay pipes. */
enum upipe_delay_command {
    UPIPE_DELAY_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current delay being set into urefs (int64_t **) */
    UPIPE_DELAY_GET_DELAY,
    /** sets the delay to set into urefs (int64_t *) */
    UPIPE_DELAY_SET_DELAY
};

/** @This returns the management structure for all delay pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_delay_mgr_alloc(void);

/** @This returns the current delay being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled with the current delay
 * @return an error code
 */
static inline int upipe_delay_get_delay(struct upipe *upipe, int64_t *delay_p)
{
    return upipe_control(upipe, UPIPE_DELAY_GET_DELAY,
                         UPIPE_DELAY_SIGNATURE, delay_p);
}

/** @This sets the delay to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param delay delay to set
 * @return an error code
 */
static inline int upipe_delay_set_delay(struct upipe *upipe, int64_t delay)
{
    return upipe_control(upipe, UPIPE_DELAY_SET_DELAY,
                         UPIPE_DELAY_SIGNATURE, delay);
}

#ifdef __cplusplus
}
#endif
#endif

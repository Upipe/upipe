/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short Upipe linear module sending output identical to input
 */

#ifndef _UPIPE_MODULES_UPIPE_DVEO_ASI_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_DVEO_ASI_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_DVEO_ASI_SINK_SIGNATURE UBASE_FOURCC('d','v','a','k')

/** @This extends upipe_command with specific commands. */
enum upipe_dveo_asi_sink_command {
    UPIPE_DVEO_ASI_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the uclock (struct uclock *) **/
    UPIPE_DVEO_ASI_SINK_GET_UCLOCK,
};

/** @This returns the dveo asi uclock.
 *
 * @param upipe description structure of the super pipe
 * @param uclock_p filled in with a pointer to the uclock
 * @return an error code
 */
static inline int upipe_dveo_asi_sink_get_uclock(struct upipe *upipe,
                                              struct uclock **uclock_p)
{
    return upipe_control(upipe, UPIPE_DVEO_ASI_SINK_GET_UCLOCK,
                          UPIPE_DVEO_ASI_SINK_SIGNATURE, uclock_p);
}


/** @This returns the management structure for dveo_asi_sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dveo_asi_sink_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

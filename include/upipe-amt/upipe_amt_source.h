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
 * @short Upipe source module for automatic multicat tunneling
 */

#ifndef _UPIPE_AMT_UPIPE_AMT_SOURCE_H_
/** @hidden */
#define _UPIPE_AMT_UPIPE_AMT_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AMTSRC_SIGNATURE UBASE_FOURCC('a','m','t','c')

/** @This returns the management structure for all amtsrc pipes.
 *
 * @param amt_relay IP of the AMT relay
 * @return pointer to manager
 */
struct upipe_mgr *upipe_amtsrc_mgr_alloc(const char *amt_relay);

/** @This extends upipe_mgr_command with specific commands for amtsrc. */
enum upipe_amtsrc_mgr_command {
    UPIPE_AMTSRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** sets the timeout to switch to AMT (unsigned int) */
    UPIPE_AMTSRC_MGR_SET_TIMEOUT
};

/** @This sets the timeout to switch from SSM to AMT.
 *
 * @param mgr pointer to manager
 * @param timeout timeout in seconds
 * @return an error code
 */
static inline int
    upipe_amtsrc_mgr_set_timeout(struct upipe_mgr *mgr, unsigned int timeout)
{
    return upipe_mgr_control(mgr, UPIPE_AMTSRC_MGR_SET_TIMEOUT,
                             UPIPE_AMTSRC_SIGNATURE, timeout);
}

#ifdef __cplusplus
}
#endif
#endif

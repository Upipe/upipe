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
 * @short Upipe module setting arbitrary attributes to urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_SETRAP_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SETRAP_H_

#include <upipe/upipe.h>

#define UPIPE_SETRAP_SIGNATURE UBASE_FOURCC('s','r','a','p')

/** @This extends upipe_command with specific commands for setrap pipes. */
enum upipe_setrap_command {
    UPIPE_SETRAP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current rap being set into urefs (struct uref **) */
    UPIPE_SETRAP_GET_RAP,
    /** sets the rap to set into urefs (struct uref *) */
    UPIPE_SETRAP_SET_RAP
};

/** @This returns the management structure for all setrap pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_setrap_mgr_alloc(void);

/** @This returns the current systime_rap being set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap_p filled with the current systime_rap
 * @return false in case of error
 */
static inline bool upipe_setrap_get_rap(struct upipe *upipe, uint64_t *rap_p)
{
    return upipe_control(upipe, UPIPE_SETRAP_GET_RAP,
                         UPIPE_SETRAP_SIGNATURE, rap_p);
}

/** @This sets the systime_rap to set into urefs.
 *
 * @param upipe description structure of the pipe
 * @param rap systime_rap to set
 * @return false in case of error
 */
static inline bool upipe_setrap_set_rap(struct upipe *upipe, uint64_t rap)
{
    return upipe_control(upipe, UPIPE_SETRAP_SET_RAP,
                         UPIPE_SETRAP_SIGNATURE, rap);
}

#endif

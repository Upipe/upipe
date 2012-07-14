/*****************************************************************************
 * upipe_sws.h: application interface for sws module
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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
 *****************************************************************************/

#ifndef _UPIPE_MODULES_UPIPE_SWS_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SWS_H_

#include <upipe/upipe.h>

#define UPIPE_SWS_SIGNATURE 0x0F020001U

/** @This sets a new output flow definition along with the
 * output picture size
 *
 * @param upipe description structure of the pipe
 * @param flow output flow definition
 * @param hsize horizontal size
 * @param vsize vertical size
 * @return false in case of error
 */
bool upipe_sws_set_out_flow(struct upipe *upipe, struct uref* flow, uint64_t hsize, uint64_t vsize);

/** @This returns the management structure for sws pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_mgr_alloc(void);

#endif

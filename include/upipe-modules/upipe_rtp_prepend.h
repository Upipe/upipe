/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe rtp module to prepend rtp header to uref blocks
 */

#ifndef _UPIPE_MODULES_UPIPE_RTP_PREPEND_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_RTP_PREPEND_H_

#include <stdint.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>

#define UPIPE_RTP_PREPEND_SIGNATURE UBASE_FOURCC('r','t','p','p')

/** @This extends upipe_command with specific commands for rtp_prepend pipes. */
enum upipe_rtp_prepend_command {
    UPIPE_RTP_PREPEND_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set rtp type/clockrate (uint8_t, uint32_t) */
    UPIPE_RTP_PREPEND_SET_TYPE,
    /** set rtp type/clockrate (uint8_t*, uint32_t*) */
    UPIPE_RTP_PREPEND_GET_TYPE,
};

/** @This returns the configured RTP type.
 *
 * @param upipe description structure of the pipe
 * @param type_p rtp type
 * @param clockrate_p rtp timestamp clock rate
 * @return false in case of error
 */
static inline bool upipe_rtp_prepend_get_type(struct upipe *upipe,
                               uint8_t *type_p, uint32_t *clockrate_p)
{
    return upipe_control(upipe, UPIPE_RTP_PREPEND_GET_TYPE,
                         UPIPE_RTP_PREPEND_SIGNATURE, type_p, clockrate_p);
}

/** @This sets the RTP type.
 *
 * @param upipe description structure of the pipe
 * @param type rtp payload type
 * @param clockrate rtp timestamp and clock rate (optional, set
 * according to rfc 3551 if null)
 * @return false in case of error
 */
static inline bool upipe_rtp_prepend_set_type(struct upipe *upipe,
                               uint8_t type, uint32_t clockrate)
{
    return upipe_control(upipe, UPIPE_RTP_PREPEND_SET_TYPE,
                         UPIPE_RTP_PREPEND_SIGNATURE, type, clockrate);
}

/** @This returns the management structure for rtp_prepend pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_prepend_mgr_alloc(void);

#endif

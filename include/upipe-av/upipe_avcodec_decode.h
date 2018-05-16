/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe avcodec decode wrapper module
 */

#ifndef _UPIPE_AV_UPIPE_AVCODEC_DECODE_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AVCODEC_DECODE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AVCDEC_SIGNATURE UBASE_FOURCC('a', 'v', 'c', 'd')

/** @This extends upipe_command with specific commands for upipe_avcdec. */
enum upipe_avcdec_command {
    UPIPE_AVCDEC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set hardware config (const char *, const char *) */
    UPIPE_AVCDEC_SET_HW_CONFIG,
};

/** @This sets the hardware accel configuration.
 *
 * @param upipe description structure of the pipe
 * @param type hardware acceleration type
 * @param device hardware device to open (use NULL for default)
 * @return an error code
 */
static inline int upipe_avcdec_set_hw_config(struct upipe *upipe,
                                             const char *type,
                                             const char *device)
{
    return upipe_control(upipe, UPIPE_AVCDEC_SET_HW_CONFIG,
                         UPIPE_AVCDEC_SIGNATURE, type, device);
}

/** @This returns the management structure for all avcodec decode pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avcdec_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

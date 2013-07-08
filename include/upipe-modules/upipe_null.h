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
 * @short Upipe null module - free incoming urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_NULL_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NULL_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_NULL_SIGNATURE UBASE_FOURCC('n', 'u', 'l', 'l')

/** @This extends upipe_command with specific commands for null */
enum upipe_null_command {
    UPIPE_NULL_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** ask enable or disable dumping dicts (int) */
    UPIPE_NULL_DUMP_DICT
};

/** @This enables or disables dumping of uref->udict
 *
 * @param upipe description structure of the pipe
 * @param enable enable or disable
 * @return false in case of error
 */
static inline bool upipe_null_dump_dict(struct upipe *upipe, bool enable)
{
    return upipe_control(upipe, UPIPE_NULL_DUMP_DICT, UPIPE_NULL_SIGNATURE, (enable ? 1 : 0));
}

/** @This returns the management structure for null pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_null_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

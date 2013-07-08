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
 * @short Upipe module - skip
 * Skip arbitrary length of data in blocks.
 */

#ifndef _UPIPE_MODULES_UPIPE_SKIP_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SKIP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_SKIP_SIGNATURE UBASE_FOURCC('s','k','i','p')

/** @This extends upipe_command with specific commands for upipe_skip. */
enum upipe_skip_command {
    UPIPE_SKIP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current offset (size_t *) */
    UPIPE_SKIP_GET_OFFSET,
    /**  defines offset (size_t) */
    UPIPE_SKIP_SET_OFFSET,
};

/** @This returns the currently opened offset.
 *
 * @param upipe description structure of the pipe
 * @param offset current offset
 * @return false in case of error
 */
static inline bool upipe_skip_get_offset(struct upipe *upipe, size_t *offset_p)
{
    return upipe_control(upipe, UPIPE_SKIP_GET_OFFSET, UPIPE_SKIP_SIGNATURE,
                         offset_p);
}

/** @This asks to open the given offset.
 *
 * @param upipe description structure of the pipe
 * @param offset skip offset
 * @return false in case of error
 */
static inline bool upipe_skip_set_offset(struct upipe *upipe, size_t offset)
{
    return upipe_control(upipe, UPIPE_SKIP_SET_OFFSET, UPIPE_SKIP_SIGNATURE,
                         offset);
}

/** @This returns the management structure for skip pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_skip_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

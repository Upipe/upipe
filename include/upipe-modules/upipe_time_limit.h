/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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
 * @short Upipe module blocking sources if they are too early
 */

#ifndef _UPIPE_MODULES_UPIPE_TIME_LIMIT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_TIME_LIMIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_TIME_LIMIT_SIGNATURE UBASE_FOURCC('t','m','l','t')

/** @This extends @ref upipe_command with specific commands for
 * time limit pipe.
 */
enum upipe_time_limit_command {
    UPIPE_TIME_LIMIT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the time limit (uint64_t) in @ref #UCLOCK_FREQ units */
    UPIPE_TIME_LIMIT_SET_LIMIT,
    /** get the time limit (uint64_t *) in @ref #UCLOCK_FREQ units */
    UPIPE_TIME_LIMIT_GET_LIMIT
};

/** @This converts @ref upipe_time_limit_command to a string.
 *
 * @param command command to convert
 * @return a string of NULL if invalid
 */
static inline const char *upipe_time_limit_command_str(int command)
{
    switch ((enum upipe_time_limit_command)command) {
        UBASE_CASE_TO_STR(UPIPE_TIME_LIMIT_GET_LIMIT);
        UBASE_CASE_TO_STR(UPIPE_TIME_LIMIT_SET_LIMIT);
        case UPIPE_TIME_LIMIT_SENTINEL: break;
    }
    return NULL;
}

/** @This sets the time limit.
 *
 * @param upipe description structure of the pipe
 * @param time_limit the time limit in @ref #UCLOCK_FREQ units
 * @return an error code
 */
static inline int upipe_time_limit_set_limit(struct upipe *upipe,
                                             uint64_t time_limit)
{
    return upipe_control(upipe, UPIPE_TIME_LIMIT_SET_LIMIT,
                         UPIPE_TIME_LIMIT_SIGNATURE, time_limit);
}

/** @This gets the time limit.
 *
 * @param upipe description structure of the pipe
 * @param time_limit_p a pointer to the time limit to get in @ref #UCLOCK_FREQ
 * units
 * @return an error code
 */
static inline int upipe_time_limit_get_limit(struct upipe *upipe,
                                             uint64_t *time_limit_p)
{
    return upipe_control(upipe, UPIPE_TIME_LIMIT_GET_LIMIT,
                         UPIPE_TIME_LIMIT_SIGNATURE, time_limit_p);
}

/** @This returns the management structure for all time_limit pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_time_limit_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short Upipe rate limit module
 */

#ifndef _UPIPE_MODULES_UPIPE_RATE_LIMIT_H_
# define _UPIPE_MODULES_UPIPE_RATE_LIMIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_RATE_LIMIT_SIGNATURE UBASE_FOURCC('r','t','l','t')

/** @This extends @ref upipe_command with specific commands for
 * rate limit pipe.
 */
enum upipe_rate_limit_command {
    UPIPE_RATE_LIMIT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the rate limit (uint64_t) in octet per second */
    UPIPE_RATE_LIMIT_SET_LIMIT,
    /** get the rate limit (uint64_t *) in octet per second */
    UPIPE_RATE_LIMIT_GET_LIMIT,
    /** set the window (uint64_t) in clock tick */
    UPIPE_RATE_LIMIT_SET_DURATION,
    /** get the window (uint64_t *) in clock tick */
    UPIPE_RATE_LIMIT_GET_DURATION,
};

/** @This converts @ref upipe_rate_limit_command to a string.
 *
 * @param command command to convert
 * @return a string of NULL if invalid
 */
static inline const char *upipe_rate_limit_command_str(int command)
{
    switch ((enum upipe_rate_limit_command)command) {
    UBASE_CASE_TO_STR(UPIPE_RATE_LIMIT_GET_LIMIT);
    UBASE_CASE_TO_STR(UPIPE_RATE_LIMIT_SET_LIMIT);
    UBASE_CASE_TO_STR(UPIPE_RATE_LIMIT_GET_DURATION);
    UBASE_CASE_TO_STR(UPIPE_RATE_LIMIT_SET_DURATION);
    case UPIPE_RATE_LIMIT_SENTINEL: break;
    }
    return NULL;
}

/** @This sets the rate limit.
 *
 * @param upipe description structure of the pipe
 * @param rate_limit the rate limit in octet per second
 * @return an error code
 */
static inline int upipe_rate_limit_set_limit(struct upipe *upipe,
                                             uint64_t rate_limit)
{
    return upipe_control(upipe, UPIPE_RATE_LIMIT_SET_LIMIT,
                         UPIPE_RATE_LIMIT_SIGNATURE, rate_limit);
}

/** @This gets the rate limit.
 *
 * @param upipe description structure of the pipe
 * @param rate_limit_p a pointer to the rate limit to get in octet per second
 * @return an error code
 */
static inline int upipe_rate_limit_get_limit(struct upipe *upipe,
                                             uint64_t *rate_limit_p)
{
    return upipe_control(upipe, UPIPE_RATE_LIMIT_GET_LIMIT,
                         UPIPE_RATE_LIMIT_SIGNATURE, rate_limit_p);
}

/** @This sets the rate limit window.
 *
 * @param upipe description structure of the pipe
 * @param duration window duration in clock tick
 * @return an error code
 */
static inline int upipe_rate_limit_set_duration(struct upipe *upipe,
                                                uint64_t duration)
{
    return upipe_control(upipe, UPIPE_RATE_LIMIT_SET_DURATION,
                         UPIPE_RATE_LIMIT_SIGNATURE, duration);
}

/** @This gets the rate limit window.
 *
 * @param upipe description structure of the pipe
 * @param duration_p pointer filled with the window duration in clock tick
 * @return an error code
 */
static inline int upipe_rate_limit_get_duration(struct upipe *upipe,
                                                uint64_t *duration_p)
{
    return upipe_control(upipe, UPIPE_RATE_LIMIT_GET_DURATION,
                         UPIPE_RATE_LIMIT_SIGNATURE, duration_p);
}

/** @This returns the rate limit pipe manager. */
struct upipe_mgr *upipe_rate_limit_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

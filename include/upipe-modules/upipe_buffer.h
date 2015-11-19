/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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
 * @short Upipe buffer module
 *
 * The buffer pipe directly forwards the input uref if it can. When the output
 * upump is blocked by the output pipe, the buffer pipe still accepts the input
 * uref until the maximum size is reached.
 */

#ifndef _UPIPE_MODULES_UPIPE_BUFFER_H_
/** @hidden */
# define _UPIPE_MODULES_UPIPE_BUFFER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_BUFFER_SIGNATURE UBASE_FOURCC('b','u','f','f')

/** @This extends @ref upipe_command with specific buffer commands. */
enum upipe_buffer_command {
    UPIPE_BUFFER_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the maximum retained size in octet (uint64_t) */
    UPIPE_BUFFER_SET_MAX_SIZE,
    /** get the maximum retained size in octet (uint64_t *) */
    UPIPE_BUFFER_GET_MAX_SIZE,
    /** set the low limit in octet (uint64_t) */
    UPIPE_BUFFER_SET_LOW,
    /** get the low limit in octet (uint64_t *) */
    UPIPE_BUFFER_GET_LOW,
    /** set the high limit in octet (uint64_t) */
    UPIPE_BUFFER_SET_HIGH,
    /** get the high limit in octet (uint64_t *) */
    UPIPE_BUFFER_GET_HIGH,
};

/** @This converts @ref upipe_buffer_command to a string.
 *
 * @param command command to convert
 * @return a string or NULL if invalid
 */
static inline const char *upipe_buffer_command_str(int command)
{
    switch ((enum upipe_buffer_command)command) {
    UBASE_CASE_TO_STR(UPIPE_BUFFER_SET_MAX_SIZE);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_GET_MAX_SIZE);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_SET_LOW);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_GET_LOW);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_SET_HIGH);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_GET_HIGH);
    case UPIPE_BUFFER_SENTINEL: break;
    }
    return NULL;
}

/** @This gets the maximum retained size in octet.
 *
 * @param upipe description structure of the pipe
 * @param max_size_p a pointer to the size
 * @return an error code
 */
static inline int upipe_buffer_get_max_size(struct upipe *upipe,
                                            uint64_t *max_size_p)
{
    return upipe_control(upipe, UPIPE_BUFFER_GET_MAX_SIZE,
                         UPIPE_BUFFER_SIGNATURE, max_size_p);
}

/** @This sets the maximum retained size in octet.
 *
 * @param upipe description structure of the pipe
 * @param max_size the maximum size in octet
 * @return an error code
 */
static inline int upipe_buffer_set_max_size(struct upipe *upipe,
                                            uint64_t max_size)
{
    return upipe_control(upipe, UPIPE_BUFFER_SET_MAX_SIZE,
                         UPIPE_BUFFER_SIGNATURE, max_size);
}

/** @This sets the low limit size in octet.
 *
 * @param upipe description structure of the pipe
 * @param low_limit the low limit size in octet
 * @return an error code
 */
static inline int upipe_buffer_set_low_limit(struct upipe *upipe,
                                             uint64_t low_limit)
{
    return upipe_control(upipe, UPIPE_BUFFER_SET_LOW,
                         UPIPE_BUFFER_SIGNATURE, low_limit);
}

/** @This gets the low limit size in octet.
 *
 * @param upipe description structure of the pipe
 * @param low_limit_p a point to the low limit size in octet
 * @return an error code
 */
static inline int upipe_buffer_get_low_limit(struct upipe *upipe,
                                             uint64_t *low_limit_p)
{
    return upipe_control(upipe, UPIPE_BUFFER_GET_LOW,
                         UPIPE_BUFFER_SIGNATURE, low_limit_p);
}

/** @This sets the high limit size in octet.
 *
 * @param upipe description structure of the pipe
 * @param high_limit the high limit size in octet
 * @return an error code
 */
static inline int upipe_buffer_set_high_limit(struct upipe *upipe,
                                              uint64_t high_limit)
{
    return upipe_control(upipe, UPIPE_BUFFER_SET_HIGH,
                         UPIPE_BUFFER_SIGNATURE, high_limit);
}

/** @This gets the high limit size in octet.
 *
 * @param upipe description structure of the pipe
 * @param high_limit_p a point to the high limit size in octet
 * @return an error code
 */
static inline int upipe_buffer_get_high_limit(struct upipe *upipe,
                                              uint64_t *high_limit_p)
{
    return upipe_control(upipe, UPIPE_BUFFER_GET_HIGH,
                         UPIPE_BUFFER_SIGNATURE, high_limit_p);
}

/** @This is the buffer pipe states. */
enum upipe_buffer_state {
    /** under the low limit */
    UPIPE_BUFFER_LOW,
    /** between low and high limit */
    UPIPE_BUFFER_MIDDLE,
    /** above high limit */
    UPIPE_BUFFER_HIGH,
};

/** @This converts @ref upipe_buffer_state to a string.
 *
 * @param s buffer state
 * @return a string or NULL if invalid
 */
static inline const char *upipe_buffer_state_str(enum upipe_buffer_state s)
{
    switch (s) {
    UBASE_CASE_TO_STR(UPIPE_BUFFER_LOW);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_MIDDLE);
    UBASE_CASE_TO_STR(UPIPE_BUFFER_HIGH);
    }
    return NULL;
}

/** @This extends @ref uprobe_event with specific buffer events. */
enum upipe_buffer_event {
    UPROBE_BUFFER_SENTINEL = UPROBE_LOCAL,

    /** upipe buffer state changed */
    UPROBE_BUFFER_UPDATE,
};

/** @This converts @ref upipe_buffer_event to a string.
 *
 * @param event event to convert
 * @return a string or NULL if invalid
 */
static inline const char *upipe_buffer_event_str(int event)
{
    switch ((enum upipe_buffer_event)event) {
    UBASE_CASE_TO_STR(UPROBE_BUFFER_UPDATE);
    case UPROBE_BUFFER_SENTINEL: break;
    }
    return NULL;
}

/** @This return the buffer pipe manager.
 *
 * @return a pointer to the manager
 */
struct upipe_mgr *upipe_buffer_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

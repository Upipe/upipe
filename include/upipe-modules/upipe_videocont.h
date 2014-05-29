/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module video continuity
 */

#ifndef _UPIPE_TS_UPIPE_VIDEOCONT_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_VIDEOCONT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_VIDEOCONT_SIGNATURE UBASE_FOURCC('v','i','d','c')
#define UPIPE_VIDEOCONT_SUB_SIGNATURE UBASE_FOURCC('v','i','d','i')

/** @This extends upipe_command with specific commands for upipe_videocont
 * pipes. */
enum upipe_videocont_command {
    UPIPE_VIDEOCONT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current input name if any (const char **) */
    UPIPE_VIDEOCONT_GET_CURRENT_INPUT,
    /** returns the (next) input name (const char **) */
    UPIPE_VIDEOCONT_GET_INPUT,
    /** sets the grid input by its name (const char *) */
    UPIPE_VIDEOCONT_SET_INPUT,
    /** returns the current pts tolerance (int *) */
    UPIPE_VIDEOCONT_GET_TOLERANCE,
    /** sets the pts tolerance (int) */
    UPIPE_VIDEOCONT_SET_TOLERANCE,
};

/** @This extends upipe_command with specific commands for upipe_videocont
 * subpipes. */
enum upipe_videocont_sub_command {
    UPIPE_VIDEOCONT_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** This sets a videocont subpipe as its grandpipe input () */
    UPIPE_VIDEOCONT_SUB_SET_INPUT,
};

/** @This returns the current input name if any.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with current input name pointer or NULL
 * @return an error code
 */
static inline enum ubase_err upipe_videocont_get_current_input(
                       struct upipe *upipe, const char **name_p)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_GET_CURRENT_INPUT,
                         UPIPE_VIDEOCONT_SIGNATURE, name_p);
}

/** @This returns the (next) input name.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with (next) input name pointer or NULL
 * @return an error code
 */
static inline enum ubase_err upipe_videocont_get_input(struct upipe *upipe,
                                                       const char **name_p)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_GET_INPUT,
                         UPIPE_VIDEOCONT_SIGNATURE, name_p);
}

/** @This sets the grid input by its name.
 *
 * @param upipe description structure of the pipe
 * @param name input name
 * @return an error code
 */
static inline enum ubase_err upipe_videocont_set_input(struct upipe *upipe,
                                                       const char *name)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_SET_INPUT,
                         UPIPE_VIDEOCONT_SIGNATURE, name);
}

/** @This returns the current pts tolerance.
 *
 * @param upipe description structure of the pipe
 * @param tolerance_p filled with current pts tolerance
 * @return an error code
 */
static inline enum ubase_err upipe_videocont_get_tolerance(struct upipe *upipe,
                                                       uint64_t *tolerance_p)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_GET_TOLERANCE,
                         UPIPE_VIDEOCONT_SIGNATURE, tolerance_p);
}

/** @This sets the pts tolerance.
 *
 * @param upipe description structure of the pipe
 * @param tolerance pts tolerance
 * @return an error code
 */
static inline enum ubase_err upipe_videocont_set_tolerance(struct upipe *upipe,
                                                       uint64_t tolerance)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_SET_TOLERANCE,
                         UPIPE_VIDEOCONT_SIGNATURE, tolerance);
}

/** @This sets a videocont subpipe as its grandpipe input.
 *
 * @param upipe description structure of the (sub)pipe
 * @return an error code
 */
static inline enum ubase_err upipe_videocont_sub_set_input(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_SUB_SET_INPUT,
                         UPIPE_VIDEOCONT_SUB_SIGNATURE);
}

/** @This returns the management structure for all ts_join pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_videocont_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

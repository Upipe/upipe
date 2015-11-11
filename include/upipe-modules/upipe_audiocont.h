/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module audio continuity
 */

#ifndef _UPIPE_MODULES_UPIPE_AUDIOCONT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_AUDIOCONT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AUDIOCONT_SIGNATURE UBASE_FOURCC('a','u','d','c')
#define UPIPE_AUDIOCONT_SUB_SIGNATURE UBASE_FOURCC('a','u','d','i')

/** @This extends upipe_command with specific commands for upipe_audiocont
 * pipes. */
enum upipe_audiocont_command {
    UPIPE_AUDIOCONT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the current input name if any (const char **) */
    UPIPE_AUDIOCONT_GET_CURRENT_INPUT,
    /** returns the (next) input name (const char **) */
    UPIPE_AUDIOCONT_GET_INPUT,
    /** sets the grid input by its name (const char *) */
    UPIPE_AUDIOCONT_SET_INPUT,
    /** returns the current pts latency (uint64_t *) */
    UPIPE_AUDIOCONT_GET_LATENCY,
    /** sets the pts latency (uint64_t) */
    UPIPE_AUDIOCONT_SET_LATENCY,
    /** returns the current crossblend period (uint64_t *) */
    UPIPE_AUDIOCONT_GET_CROSSBLEND,
    /** sets the crossblend period (uint64_t) */
    UPIPE_AUDIOCONT_SET_CROSSBLEND
};

/** @This extends upipe_command with specific commands for upipe_audiocont
 * subpipes. */
enum upipe_audiocont_sub_command {
    UPIPE_AUDIOCONT_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** This sets a audiocont subpipe as its grandpipe input (void) */
    UPIPE_AUDIOCONT_SUB_SET_INPUT
};

/** @This returns the current input name if any.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with current input name pointer or NULL
 * @return an error code
 */
static inline int upipe_audiocont_get_current_input(struct upipe *upipe,
                                                    const char **name_p)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_GET_CURRENT_INPUT,
                         UPIPE_AUDIOCONT_SIGNATURE, name_p);
}

/** @This returns the (next) input name.
 *
 * @param upipe description structure of the pipe
 * @param name_p filled with (next) input name pointer or NULL
 * @return an error code
 */
static inline int upipe_audiocont_get_input(struct upipe *upipe,
                                            const char **name_p)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_GET_INPUT,
                         UPIPE_AUDIOCONT_SIGNATURE, name_p);
}

/** @This sets the grid input by its name.
 *
 * @param upipe description structure of the pipe
 * @param name input name
 * @return an error code
 */
static inline int upipe_audiocont_set_input(struct upipe *upipe,
                                            const char *name)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_SET_INPUT,
                         UPIPE_AUDIOCONT_SIGNATURE, name);
}

/** @This returns the current pts latency.
 *
 * @param upipe description structure of the pipe
 * @param latency_p filled with current pts latency
 * @return an error code
 */
static inline int upipe_audiocont_get_latency(struct upipe *upipe,
                                              uint64_t *latency_p)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_GET_LATENCY,
                         UPIPE_AUDIOCONT_SIGNATURE, latency_p);
}

/** @This sets the pts latency.
 *
 * @param upipe description structure of the pipe
 * @param latency pts latency
 * @return an error code
 */
static inline int upipe_audiocont_set_latency(struct upipe *upipe,
                                              uint64_t latency)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_SET_LATENCY,
                         UPIPE_AUDIOCONT_SIGNATURE, latency);
}

/** @This returns the current crossblend period.
 *
 * @param upipe description structure of the pipe
 * @param crossblend_p filled with current crossblend period
 * @return an error code
 */
static inline int upipe_audiocont_get_crossblend(struct upipe *upipe,
                                                 uint64_t *crossblend_p)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_GET_CROSSBLEND,
                         UPIPE_AUDIOCONT_SIGNATURE, crossblend_p);
}

/** @This sets the crossblend period.
 *
 * @param upipe description structure of the pipe
 * @param crossblend crossblend period
 * @return an error code
 */
static inline int upipe_audiocont_set_crossblend(struct upipe *upipe,
                                                 uint64_t crossblend)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_SET_CROSSBLEND,
                         UPIPE_AUDIOCONT_SIGNATURE, crossblend);
}

/** @This sets a audiocont subpipe as its grandpipe input.
 *
 * @param upipe description structure of the (sub)pipe
 * @return an error code
 */
static inline int upipe_audiocont_sub_set_input(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_AUDIOCONT_SUB_SET_INPUT,
                         UPIPE_AUDIOCONT_SUB_SIGNATURE);
}

/** @This returns the management structure for all ts_join pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_audiocont_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

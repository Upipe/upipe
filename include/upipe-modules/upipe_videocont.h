/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT

 */

/** @file
 * @short Upipe module video continuity
 */

#ifndef _UPIPE_MODULES_UPIPE_VIDEOCONT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_VIDEOCONT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

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
    /** returns the current pts latency (uint64_t *) */
    UPIPE_VIDEOCONT_GET_LATENCY,
    /** sets the pts latency (uint64_t) */
    UPIPE_VIDEOCONT_SET_LATENCY,
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
static inline int upipe_videocont_get_current_input(struct upipe *upipe,
                                                    const char **name_p)
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
static inline int upipe_videocont_get_input(struct upipe *upipe,
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
static inline int upipe_videocont_set_input(struct upipe *upipe,
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
static inline int upipe_videocont_get_tolerance(struct upipe *upipe,
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
static inline int upipe_videocont_set_tolerance(struct upipe *upipe,
                                                uint64_t tolerance)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_SET_TOLERANCE,
                         UPIPE_VIDEOCONT_SIGNATURE, tolerance);
}

/** @This returns the current pts latency.
 *
 * @param upipe description structure of the pipe
 * @param latency_p filled with current pts latency
 * @return an error code
 */
static inline int upipe_videocont_get_latency(struct upipe *upipe,
                                              uint64_t *latency_p)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_GET_LATENCY,
                         UPIPE_VIDEOCONT_SIGNATURE, latency_p);
}

/** @This sets the pts latency.
 *
 * @param upipe description structure of the pipe
 * @param latency pts latency
 * @return an error code
 */
static inline int upipe_videocont_set_latency(struct upipe *upipe,
                                              uint64_t latency)
{
    return upipe_control(upipe, UPIPE_VIDEOCONT_SET_LATENCY,
                         UPIPE_VIDEOCONT_SIGNATURE, latency);
}

/** @This sets a videocont subpipe as its grandpipe input.
 *
 * @param upipe description structure of the (sub)pipe
 * @return an error code
 */
static inline int upipe_videocont_sub_set_input(struct upipe *upipe)
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

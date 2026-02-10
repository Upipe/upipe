/*
 * Copyright (C) 2026 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe interlacing module
 */

#ifndef _UPIPE_MODULES_UPIPE_INTERLACE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_INTERLACE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_INTERLACE_SIGNATURE UBASE_FOURCC('i','n','t','l')

/** @This extends upipe_command with specific commands for interlace pipes. */
enum upipe_interlace_command {
    UPIPE_INTERLACE_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set top field first output (bool) */
    UPIPE_INTERLACE_SET_TFF,
    /** get the configured value for top field first output (bool *) */
    UPIPE_INTERLACE_GET_TFF,
    /** set field drop (bool) */
    UPIPE_INTERLACE_SET_DROP,
    /** get the configured value for field top output (bool *) */
    UPIPE_INTERLACE_GET_DROP,
};

/** @This converts @ref upipe_interlace_command to a string.
 *
 * @param command command to convert
 * @return a string or NULL if invalid
 */
static inline const char *upipe_interlace_command_str(int command)
{
    switch ((enum upipe_interlace_command)command) {
        UBASE_CASE_TO_STR(UPIPE_INTERLACE_SET_TFF);
        UBASE_CASE_TO_STR(UPIPE_INTERLACE_GET_TFF);
        UBASE_CASE_TO_STR(UPIPE_INTERLACE_SET_DROP);
        UBASE_CASE_TO_STR(UPIPE_INTERLACE_GET_DROP);
        case UPIPE_INTERLACE_SENTINEL: break;
    }
    return NULL;
}

/** @This sets the top field first output.
 *
 * @param upipe description structure of the pipe
 * @param tff true for top field first, false for bottom field first
 * @return an error code
 */
static inline int upipe_interlace_set_tff(struct upipe *upipe, bool tff)
{
    return upipe_control(upipe, UPIPE_INTERLACE_SET_TFF,
                         UPIPE_INTERLACE_SIGNATURE, tff ? 1 : 0);
}

/** @This get the top field first output configuration.
 *
 * @param upipe description structure of the pipe
 * @param tff filled with the configured value
 * @return an error code
 */
static inline int upipe_interlace_get_tff(struct upipe *upipe, bool *tff)
{
    return upipe_control(upipe, UPIPE_INTERLACE_GET_TFF,
                         UPIPE_INTERLACE_SIGNATURE, tff);
}

/** @This sets field drop.
 *
 * If set to true, two frames are merged into one, keeping one field
 * of each, so the output frame rate will be divided by two.
 *
 * 1111  2222  3333  4444  -> 1111  3333
 *  1111  2222  3333  4444     2222  4444
 * 1111  2222  3333  4444     1111  3333
 *  1111  2222  3333  4444     2222  4444
 *
 * If set to false, each frame is merged with the previous and the
 * next so the output frame rate is unchanged.
 *
 * 1111  2222  3333  4444  -> 1111  2222  3333  4444
 *  1111  2222  3333  4444     2222  3333  4444  5555
 * 1111  2222  3333  4444     1111  2222  3333  4444
 *  1111  2222  3333  4444     2222  3333  4444  5555
 *
 * @param upipe description structure of the pipe
 * @param drop true for dropping field
 * @return an error code
 */
static inline int upipe_interlace_set_drop(struct upipe *upipe, bool drop)
{
    return upipe_control(upipe, UPIPE_INTERLACE_SET_DROP,
                         UPIPE_INTERLACE_SIGNATURE, drop ? 1 : 0);
}

/** @This get the field drop configuration.
 *
 * @param upipe description structure of the pipe
 * @param drop filled with the configured value
 * @return an error code
 */
static inline int upipe_interlace_get_drop(struct upipe *upipe, bool *drop)
{
    return upipe_control(upipe, UPIPE_INTERLACE_GET_DROP,
                         UPIPE_INTERLACE_SIGNATURE, drop);
}

/** @This returns the management structure for all interlace pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *
upipe_interlace_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

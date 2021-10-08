/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 * Copyright (C) 2016 DVMR
 *
 * Authors: Christophe Massiot
 *          Franck Roger
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
 * @short Upipe sink module for files
 */

#ifndef _UPIPE_MODULES_UPIPE_FILE_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_FILE_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_FSINK_SIGNATURE UBASE_FOURCC('f','s','n','k')
#define UPIPE_FSINK_EXPECTED_FLOW_DEF "block."

/** @This defines file opening modes. */
enum upipe_fsink_mode {
    /** do not do anything besides opening the fd */
    UPIPE_FSINK_NONE,
    /** append to an existing file (O_CREAT + lseek(SEEK_END)) */
    UPIPE_FSINK_APPEND,
    /** overwrite an existing file, or create it (O_CREAT + ftruncate(0)) */
    UPIPE_FSINK_OVERWRITE,
    /** create a file, fail if it already exists (O_CREAT | O_EXCL) */
    UPIPE_FSINK_CREATE
};

/** @This extends upipe_command with specific commands for file sink. */
enum upipe_fsink_command {
    UPIPE_FSINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the path of the currently opened file (const char **) */
    UPIPE_FSINK_GET_PATH,
    /** asks to open the given path (const char *, enum upipe_fsink_mode) */
    UPIPE_FSINK_SET_PATH,
    /** associates a stream with the upipe (int fildes, enum upip_fsink_mode) */
    UPIPE_FSINK_SET_FD,
    /** returns the file descriptor of the currently opened file (int *) */
    UPIPE_FSINK_GET_FD,
    /** sets fdatasync period (uint64_t) */
    UPIPE_FSINK_SET_SYNC_PERIOD,
    /** gets fdatasync period (uint64_t *) */
    UPIPE_FSINK_GET_SYNC_PERIOD,

    /** outer pipes commands begin here */
    UPIPE_FSINK_CONTROL_LOCAL = UPIPE_CONTROL_LOCAL + 0x1000
};

/** @This returns the management structure for all file sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsink_mgr_alloc(void);

/** @This returns the path of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the path of the file
 * @return an error code
 */
static inline int upipe_fsink_get_path(struct upipe *upipe,
                                       const char **path_p)
{
    return upipe_control(upipe, UPIPE_FSINK_GET_PATH, UPIPE_FSINK_SIGNATURE,
                         path_p);
}

/** @This asks to open the given file.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the file
 * @param mode mode of opening the file
 * @return an error code
 */
static inline int upipe_fsink_set_path(struct upipe *upipe,
                                       const char *path,
                                       enum upipe_fsink_mode mode)
{
    return upipe_control(upipe, UPIPE_FSINK_SET_PATH, UPIPE_FSINK_SIGNATURE,
                         path, mode);
}

/** @This associates a stream with the upipe
 *
 * @param upipe description structure of the pipe
 * @param fildes file descriptor
 * @param mode mode of opening the file
 * @return an error code
 */
static inline int upipe_fsink_set_fd(struct upipe *upipe,
                                     int fildes,
                                     enum upipe_fsink_mode mode)
{
    return upipe_control(upipe, UPIPE_FSINK_SET_FD, UPIPE_FSINK_SIGNATURE,
                         fildes, mode);
}

/** @This returns the file descriptor of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param fd_p filled in with the file descriptor of the file
 * @return an error code
 */
static inline int upipe_fsink_get_fd(struct upipe *upipe,
                                     int *fd_p)
{
    return upipe_control(upipe, UPIPE_FSINK_GET_FD, UPIPE_FSINK_SIGNATURE,
                         fd_p);
}

/** @This returns the sync period.
 *
 * @param upipe description structure of the pipe
 * @param sync_period_p filled in with the sync period
 * @return an error code
 */
static inline int upipe_fsink_get_sync_period(struct upipe *upipe,
                                              uint64_t *sync_period_p)
{
    return upipe_control(upipe, UPIPE_FSINK_GET_SYNC_PERIOD,
                         UPIPE_FSINK_SIGNATURE, sync_period_p);
}

/** @This sets the sync period.
 *
 * @param upipe description structure of the pipe
 * @param sync_period sync period
 * @return an error code
 */
static inline int upipe_fsink_set_sync_period(struct upipe *upipe,
                                              uint64_t sync_period)
{
    return upipe_control(upipe, UPIPE_FSINK_SET_SYNC_PERIOD,
                         UPIPE_FSINK_SIGNATURE, sync_period);
}

#ifdef __cplusplus
}
#endif
#endif

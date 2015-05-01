/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe source module for files
 */

#ifndef _UPIPE_MODULES_UPIPE_FILE_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_FILE_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_FSRC_SIGNATURE UBASE_FOURCC('f','s','r','c')

/** @This extends upipe_command with specific commands for file source. */
enum upipe_fsrc_command {
    UPIPE_FSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the size of the currently opened file, in octets (uint64_t *) */
    UPIPE_FSRC_GET_SIZE,
    /** returns the reading position of the currently opened file, in octets
     * (uint64_t *) */
    UPIPE_FSRC_GET_POSITION,
    /** asks to read at the given position (uint64_t) */
    UPIPE_FSRC_SET_POSITION,

    /** asks to read at the given position (uint64_t),
     * the given size (uint64_t) or to the end ((uint64_t)-1) */
    UPIPE_FSRC_SET_RANGE,
    /** return the reading range of the currently opened file,
     * position (uint64_t) and length (uint64_t) */
    UPIPE_FSRC_GET_RANGE,
};

/** @This returns the management structure for all file sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fsrc_mgr_alloc(void);

/** @This returns the size of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param size_p filled in with the size of the file, in octets
 * @return an error code
 */
static inline int upipe_fsrc_get_size(struct upipe *upipe,
                                      uint64_t *size_p)
{
    return upipe_control(upipe, UPIPE_FSRC_GET_SIZE, UPIPE_FSRC_SIGNATURE,
                         size_p);
}

/** @This returns the reading position of the currently opened file.
 *
 * @param upipe description structure of the pipe
 * @param position_p filled in with the reading position, in octets
 * @return an error code
 */
static inline int upipe_fsrc_get_position(struct upipe *upipe,
                                          uint64_t *position_p)
{
    return upipe_control(upipe, UPIPE_FSRC_GET_POSITION, UPIPE_FSRC_SIGNATURE,
                         position_p);
}

/** @This asks to read at the given position.
 *
 * @param upipe description structure of the pipe
 * @param position new reading position, in octets (between 0 and the size)
 * @return an error code
 */
static inline int upipe_fsrc_set_position(struct upipe *upipe,
                                          uint64_t position)
{
    return upipe_control(upipe, UPIPE_FSRC_SET_POSITION, UPIPE_FSRC_SIGNATURE,
                         position);
}

static inline int upipe_fsrc_set_range(struct upipe *upipe,
                                       uint64_t offset,
                                       uint64_t length)
{
    return upipe_control(upipe, UPIPE_FSRC_SET_RANGE, UPIPE_FSRC_SIGNATURE,
                         offset, length);
}

static inline int upipe_fsrc_get_range(struct upipe *upipe,
                                       uint64_t *offset_p,
                                       uint64_t *length_p)
{
    return upipe_control(upipe, UPIPE_FSRC_GET_RANGE, UPIPE_FSRC_SIGNATURE,
                         offset_p, length_p);
}

#ifdef __cplusplus
}
#endif
#endif

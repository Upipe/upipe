/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module - multicat file sink
 * This sink module owns an embedded file sink and changes its path
 * depending on the uref k.systime attribute.
 */

#ifndef _UPIPE_MODULES_UPIPE_MULTICAT_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_MULTICAT_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/ubase.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_multicat_sink.h>

#define UPIPE_MULTICAT_SINK_SIGNATURE UBASE_FOURCC('m','s','n','k')
#define UPIPE_MULTICAT_SINK_DEF_ROTATE UINT64_C(97200000000)

/** @This extends upipe_command with specific commands for multicat sink. */
enum upipe_multicat_sink_command {
    UPIPE_MULTICAT_SINK_SENTINEL = UPIPE_FSINK_CONTROL_LOCAL,

    /** returns the path of the currently opened node
      * (const char **, const char **) */
    UPIPE_MULTICAT_SINK_GET_PATH,
    /** asks to open the given path (const char *, const char *) */
    UPIPE_MULTICAT_SINK_SET_PATH,
    /** asks to open the given path (enum upipe_fsink_mode) */
    UPIPE_MULTICAT_SINK_SET_MODE,
    /** get rotate interval (uint64_t *) */
    UPIPE_MULTICAT_SINK_GET_ROTATE,
    /** change rotate interval (uint64_t) */
    UPIPE_MULTICAT_SINK_SET_ROTATE,
    /** sets fsink manager (struct upipe_fsink_mgr *) */
    UPIPE_MULTICAT_SINK_SET_FSINK_MGR,
    /** gets fsink manager (struct upipe_fsink_mgr **) */
    UPIPE_MULTICAT_SINK_GET_FSINK_MGR
};

/** @This returns the management structure for multicat_sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_multicat_sink_mgr_alloc(void);

/** @This returns the path of the currently opened node.
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the path of the file
 * @return an error code
 */
static inline int
    upipe_multicat_sink_get_path(struct upipe *upipe,
                                 const char **path_p, const char **suffix_p)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_GET_PATH,
                         UPIPE_MULTICAT_SINK_SIGNATURE, path_p, suffix_p);
}

/** @This asks to open the given file.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the node
 * @return an error code
 */
static inline int
    upipe_multicat_sink_set_path(struct upipe *upipe,
                                 const char *path, const char *suffix)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_SET_PATH,
                                UPIPE_MULTICAT_SINK_SIGNATURE, path, suffix);
}

/** @This returns the rotate interval (in 27Mhz unit).
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the rotate interval in 27Mhz
 * @return an error code
 */
static inline int
    upipe_multicat_sink_get_rotate(struct upipe *upipe, uint64_t *interval_p)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_GET_ROTATE,
                                UPIPE_MULTICAT_SINK_SIGNATURE, interval_p);
}

/** @This changes the rotate interval (in 27Mhz unit)
 * (default: UPIPE_MULTICAT_SINK_DEF_ROTATE).
 *
 * @param upipe description structure of the pipe
 * @param interval rotate interval in 27Mhz
 * @return an error code
 */
static inline int
    upipe_multicat_sink_set_rotate(struct upipe *upipe, uint64_t interval)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_SET_ROTATE,
                                UPIPE_MULTICAT_SINK_SIGNATURE, interval);
}

/** @This changes the open mode starting from the next open().
 * It does NOT reopen the current file.
 * (default UPIPE_FSINK_APPEND) @see upipe_fsink_mode
 *
 * @param upipe description structure of the pipe
 * @param mode fsink mode
 * @return an error code
 */
static inline int
    upipe_multicat_sink_set_mode(struct upipe *upipe,
                                 enum upipe_fsink_mode mode)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_SET_MODE,
                                UPIPE_MULTICAT_SINK_SIGNATURE, mode);
}

/** @This returns the fsink manager.
 *
 * @param upipe description structure of the pipe
 * @param fsink_mgr fsink manager 
 * @return an error code
 */
static inline int
    upipe_multicat_sink_get_fsink_mgr(struct upipe *upipe,
                                      struct upipe_mgr *fsink_mgr)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_GET_FSINK_MGR,
                                UPIPE_MULTICAT_SINK_SIGNATURE, fsink_mgr);
}

/** @This sets the fsink manager.
 *
 * @param upipe description structure of the pipe
 * @param fsink_mgr fsink manager 
 * @return an error code
 */
static inline int
    upipe_multicat_sink_set_fsink_mgr(struct upipe *upipe,
                                      struct upipe_mgr *fsink_mgr)
{
    return upipe_control(upipe, UPIPE_MULTICAT_SINK_SET_FSINK_MGR,
                                UPIPE_MULTICAT_SINK_SIGNATURE, fsink_mgr);
}

#ifdef __cplusplus
}
#endif
#endif

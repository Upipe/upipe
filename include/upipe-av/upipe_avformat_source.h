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
 * @short Upipe source module libavformat wrapper
 */

#ifndef _UPIPE_AV_UPIPE_AVFORMAT_SOURCE_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AVFORMAT_SOURCE_H_

#include <upipe/upipe.h>

#define UPIPE_AVFSRC_SIGNATURE UBASE_FOURCC('a','v','f','r')
#define UPIPE_AVFSRC_OUTPUT_SIGNATURE UBASE_FOURCC('a','v','f','o')

/** @This extends upipe_command with specific commands for avformat source. */
enum upipe_avfsrc_command {
    UPIPE_AVFSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the content of an avformat option (const char *,
     * const char **) */
    UPIPE_AVFSRC_GET_OPTION,
    /** sets the content of an avformat option (const char *, const char *) */
    UPIPE_AVFSRC_SET_OPTION,
    /** returns the reading time of the currently opened file, in clock units
     * (uint64_t *) */
    UPIPE_AVFSRC_GET_TIME,
    /** asks to read at the given time (uint64_t) */
    UPIPE_AVFSRC_SET_TIME
};

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsrc_mgr_alloc(void);

/** @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return false in case of error
 */
static inline bool upipe_avfsrc_get_option(struct upipe *upipe,
                                           const char *option,
                                           const char **content_p)
{
    return upipe_control(upipe, UPIPE_AVFSRC_GET_OPTION, UPIPE_AVFSRC_SIGNATURE,
                         option, content_p);
}

/** @This sets the content of an avformat option. It only take effect after
 * the next call to @ref upipe_avfsrc_set_url.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return false in case of error
 */
static inline bool upipe_avfsrc_set_option(struct upipe *upipe,
                                           const char *option,
                                           const char *content)
{
    return upipe_control(upipe, UPIPE_AVFSRC_SET_OPTION, UPIPE_AVFSRC_SIGNATURE,
                         option, content);
}

/** @This returns the reading time of the currently opened URL.
 *
 * @param upipe description structure of the pipe
 * @param time_p filled in with the reading time, in clock units
 * @return false in case of error
 */
static inline bool upipe_avfsrc_get_time(struct upipe *upipe, uint64_t *time_p)
{
    return upipe_control(upipe, UPIPE_AVFSRC_GET_TIME, UPIPE_AVFSRC_SIGNATURE,
                         time_p);
}

/** @This asks to read at the given time.
 *
 * @param upipe description structure of the pipe
 * @param time new reading time, in clock units
 * @return false in case of error
 */
static inline bool upipe_avfsrc_set_time(struct upipe *upipe, uint64_t time)
{
    return upipe_control(upipe, UPIPE_AVFSRC_SET_TIME, UPIPE_AVFSRC_SIGNATURE,
                         time);
}

#endif

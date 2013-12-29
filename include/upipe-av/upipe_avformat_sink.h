/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe sink module libavformat wrapper
 */

#ifndef _UPIPE_AV_UPIPE_AVFORMAT_SINK_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AVFORMAT_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AVFSINK_SIGNATURE UBASE_FOURCC('a','v','f','k')
#define UPIPE_AVFSINK_INPUT_SIGNATURE UBASE_FOURCC('a','v','f','i')

/** @This extends upipe_command with specific commands for avformat source. */
enum upipe_avfsink_command {
    UPIPE_AVFSINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the content of an avformat option (const char *,
     * const char **) */
    UPIPE_AVFSINK_GET_OPTION,
    /** sets the content of an avformat option (const char *, const char *) */
    UPIPE_AVFSINK_SET_OPTION,
    /** returns the currently configured MIME type (const char **) */
    UPIPE_AVFSINK_GET_MIME,
    /** sets the MIME type (const char *) */
    UPIPE_AVFSINK_SET_MIME,
    /** returns the currently configured format name (const char **) */
    UPIPE_AVFSINK_GET_FORMAT,
    /** sets the format name (const char *) */
    UPIPE_AVFSINK_SET_FORMAT,
};

/** @This returns the management structure for all avformat sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsink_mgr_alloc(void);

/** @This returns the content of an avformat option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content_p filled in with the content of the option
 * @return an error code
 */
static inline enum ubase_err upipe_avfsink_get_option(struct upipe *upipe,
                                                      const char *option,
                                                      const char **content_p)
{
    return upipe_control(upipe, UPIPE_AVFSINK_GET_OPTION,
                         UPIPE_AVFSINK_SIGNATURE,
                         option, content_p);
}

/** @This sets the content of an avformat option. It only takes effect after
 * the next call to @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static inline enum ubase_err upipe_avfsink_set_option(struct upipe *upipe,
                                                      const char *option,
                                                      const char *content)
{
    return upipe_control(upipe, UPIPE_AVFSINK_SET_OPTION,
                         UPIPE_AVFSINK_SIGNATURE, option, content);
}

/** @This returns the currently configured MIME type.
 *
 * @param upipe description structure of the pipe
 * @param mime_p filled in with the currently configured MIME type
 * @return an error code
 */
static inline enum ubase_err upipe_avfsink_get_mime(struct upipe *upipe,
                                                    const char **mime_p)
{
    return upipe_control(upipe, UPIPE_AVFSINK_GET_MIME, UPIPE_AVFSINK_SIGNATURE,
                         mime_p);
}

/** @This sets the MIME type. It only takes effect after the next call to
 * @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param mime MIME type
 * @return an error code
 */
static inline enum ubase_err upipe_avfsink_set_mime(struct upipe *upipe,
                                                    const char *mime)
{
    return upipe_control(upipe, UPIPE_AVFSINK_SET_MIME, UPIPE_AVFSINK_SIGNATURE,
                         mime);
}

/** @This returns the currently configured format.
 *
 * @param upipe description structure of the pipe
 * @param format_p filled in with the currently configured format name
 * @return an error code
 */
static inline enum ubase_err upipe_avfsink_get_format(struct upipe *upipe,
                                                      const char **format_p)
{
    return upipe_control(upipe, UPIPE_AVFSINK_GET_FORMAT,
                         UPIPE_AVFSINK_SIGNATURE, format_p);
}

/** @This sets the format. It only takes effect after the next call to
 * @ref upipe_set_uri.
 *
 * @param upipe description structure of the pipe
 * @param format format name
 * @return an error code
 */
static inline enum ubase_err upipe_avfsink_set_format(struct upipe *upipe,
                                                      const char *format)
{
    return upipe_control(upipe, UPIPE_AVFSINK_SET_FORMAT,
                         UPIPE_AVFSINK_SIGNATURE, format);
}

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2013-2018 OpenHeadend S.A.R.L.
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

/** @This extends uprobe_event with specific events for avformat sink. */
enum uprobe_avfsink_event {
    UPROBE_AVFSINK_SENTINEL = UPROBE_LOCAL,

    /** offset between Upipe timestamp and avformat timestamp (uint64_t) */
    UPROBE_AVFSINK_TS_OFFSET,
};

/** @This extends upipe_command with specific commands for avformat sink. */
enum upipe_avfsink_command {
    UPIPE_AVFSINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the currently configured MIME type (const char **) */
    UPIPE_AVFSINK_GET_MIME,
    /** sets the MIME type (const char *) */
    UPIPE_AVFSINK_SET_MIME,
    /** returns the currently configured format name (const char **) */
    UPIPE_AVFSINK_GET_FORMAT,
    /** sets the format name (const char *) */
    UPIPE_AVFSINK_SET_FORMAT,
    /** returns the current duration (uint64_t *) */
    UPIPE_AVFSINK_GET_DURATION,
    /** returns the currently configured init section uri (const char **) */
    UPIPE_AVFSINK_GET_INIT_URI,
    /** sets the init section uri (const char *) */
    UPIPE_AVFSINK_SET_INIT_URI,
    /** returns timestamp offset (uint64_t *) */
    UPIPE_AVFSINK_GET_TS_OFFSET,
    /** sets the timestamp offset (uint64_t) */
    UPIPE_AVFSINK_SET_TS_OFFSET,
};

/** @This returns the management structure for all avformat sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_avfsink_mgr_alloc(void);

/** @This returns the currently configured MIME type.
 *
 * @param upipe description structure of the pipe
 * @param mime_p filled in with the currently configured MIME type
 * @return an error code
 */
static inline int upipe_avfsink_get_mime(struct upipe *upipe,
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
static inline int upipe_avfsink_set_mime(struct upipe *upipe, const char *mime)
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
static inline int upipe_avfsink_get_format(struct upipe *upipe,
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
static inline int upipe_avfsink_set_format(struct upipe *upipe,
                                           const char *format)
{
    return upipe_control(upipe, UPIPE_AVFSINK_SET_FORMAT,
                         UPIPE_AVFSINK_SIGNATURE, format);
}

/** @This returns the current duration.
 *
 * @param upipe description structure of the pipe
 * @param duration_p filled in with the current duration
 * @return an error code
 */
static inline int upipe_avfsink_get_duration(struct upipe *upipe,
                                             uint64_t *duration_p)
{
    return upipe_control(upipe, UPIPE_AVFSINK_GET_DURATION,
                         UPIPE_AVFSINK_SIGNATURE, duration_p);
}

/** @This returns the currently configured init section uri
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the current initialization section URI
 * @return an error code
 */
static inline int upipe_avfsink_get_init_uri(struct upipe *upipe,
                                             const char **uri_p)
{
    return upipe_control(upipe, UPIPE_AVFSINK_GET_INIT_URI,
                         UPIPE_AVFSINK_SIGNATURE, uri_p);
}

/** @This sets the init section uri
 *
 * @param upipe description structure of the pipe
 * @param uri initialization section URI
 * @return an error code
 */
static inline int upipe_avfsink_set_init_uri(struct upipe *upipe,
                                             const char *uri)
{
    return upipe_control(upipe, UPIPE_AVFSINK_SET_INIT_URI,
                         UPIPE_AVFSINK_SIGNATURE, uri);
}

/** @This returns the current ts_offset
 *
 * @param upipe description structure of the pipe
 * @param ts_offset_p filled with the timestamp offset
 * @return an error code
 */
static inline int upipe_avfsink_get_ts_offset(struct upipe *upipe,
                                              uint64_t *ts_offset_p)
{
    return upipe_control(upipe, UPIPE_AVFSINK_GET_TS_OFFSET,
                         UPIPE_AVFSINK_SIGNATURE, ts_offset_p);
}

/** @This sets the timestamp offset
 *
 * @param upipe description structure of the pipe
 * @param ts_offset timestamp offset
 * @return an error code
 */
static inline int upipe_avfsink_set_ts_offset(struct upipe *upipe,
                                              uint64_t ts_offset)
{
    return upipe_control(upipe, UPIPE_AVFSINK_SET_TS_OFFSET,
                         UPIPE_AVFSINK_SIGNATURE, ts_offset);
}

#ifdef __cplusplus
}
#endif
#endif

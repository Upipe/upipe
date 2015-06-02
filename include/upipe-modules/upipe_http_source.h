/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
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
 * @short Upipe source module for http GET requests
 */

#ifndef _UPIPE_MODULES_UPIPE_HTTP_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HTTP_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_HTTP_SRC_SIGNATURE UBASE_FOURCC('h','t','t','p')

/** @This extends upipe_command with specific commands for http source. */
enum upipe_http_src_command {
    UPIPE_HTTP_SRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the reading position of the current http request, in octets
     * (uint64_t *) */
    UPIPE_HTTP_SRC_GET_POSITION,
    /** asks to get at the given position (uint64_t), using Range header */
    UPIPE_HTTP_SRC_SET_POSITION,
    /** asks to get at the given position (uint64_t), the given size
     * (uint64_t), using Range header */
    UPIPE_HTTP_SRC_SET_RANGE,
};

/** @This returns the reading position of the current http request.
 *
 * @param upipe description structure of the pipe
 * @param position_p filled in with the reading position, in octets
 * @return an error code
 */
static inline int upipe_http_src_get_position(struct upipe *upipe,
                                              uint64_t *position_p)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_GET_POSITION,
                         UPIPE_HTTP_SRC_SIGNATURE, position_p);
}

/** @This request the given position using Range header
 *
 * @param upipe description structure of the pipe
 * @param position new reading position, in octets (between 0 and the size)
 * @return an error code
 */
static inline int upipe_http_src_set_position(struct upipe *upipe,
                                              uint64_t position)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_SET_POSITION,
                         UPIPE_HTTP_SRC_SIGNATURE, position);
}

/** @This request the given range
 *
 * @param upipe description structure of the pipe
 * @param offset range starts at offset, in octets
 * @param length octets to read from offset, in octets
 * @return an error code
 */
static inline int upipe_http_src_set_range(struct upipe *upipe,
                                           uint64_t offset,
                                           uint64_t length)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_SET_RANGE,
                         UPIPE_HTTP_SRC_SIGNATURE, offset, length);
}

/** @This extends uprobe_event with specific events for http source. */
enum upipe_http_src_event {
    UPROBE_HTTP_SRC_SENTINEL = UPROBE_LOCAL,

    /** request receive a redirect (302) response
     * with the url (const char *) */
    UPROBE_HTTP_SRC_REDIRECT,
};

static inline int upipe_http_src_throw_redirect(struct upipe *upipe,
                                                const char *uri)
{
    upipe_notice_va(upipe, "throw redirect to %s", uri);
    return upipe_throw(upipe, UPROBE_HTTP_SRC_REDIRECT,
                       UPIPE_HTTP_SRC_SIGNATURE, uri);
}

/** @This returns the management structure for all http sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_http_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

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

#include <upipe/upipe.h>

#define UPIPE_HTTP_SRC_SIGNATURE UBASE_FOURCC('h','t','t','p')

/** @This extends upipe_command with specific commands for http source. */
enum upipe_http_src_command {
    UPIPE_HTTP_SRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the url of the currently opened http (const char **) */
    UPIPE_HTTP_SRC_GET_URL,
    /** asks to open the given url (const char *) */
    UPIPE_HTTP_SRC_SET_URL,
};

/** @This returns the management structure for all http sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_http_src_mgr_alloc(void);

/** @This returns the url of the currently opened http.
 *
 * @param upipe description structure of the pipe
 * @param url_p filled in with the url of the http
 * @return false in case of error
 */
static inline bool upipe_http_src_get_url(struct upipe *upipe, const char **url_p)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_GET_URL, UPIPE_HTTP_SRC_SIGNATURE,
                         url_p);
}

/** @This asks to open the given http.
 *
 * @param upipe description structure of the pipe
 * @param url relative or absolute url of the http
 * @return false in case of error
 */
static inline bool upipe_http_src_set_url(struct upipe *upipe, const char *url)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_SET_URL, UPIPE_HTTP_SRC_SIGNATURE,
                         url);
}

#endif

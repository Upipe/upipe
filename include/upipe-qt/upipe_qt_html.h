/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Sebastien Gougelet
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

#ifndef _UPIPE_MODULES_UPIPE_QT_HTML_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_QT_HTML_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_QT_HTML_SIGNATURE UBASE_FOURCC('h','t','m','l')

/** @This returns the management structure for html pipes.
 *
 */
struct upipe_mgr *upipe_qt_html_mgr_alloc(void);

enum upipe_qt_html_command {
    UPIPE_QT_HTML_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_QT_HTML_GET_URL,

    UPIPE_QT_HTML_SET_URL,

    UPIPE_QT_HTML_SET_SIZE,

    UPIPE_QT_HTML_GET_SIZE,

    UPIPE_QT_HTML_SET_FRAMERATE,

    UPIPE_QT_HTML_GET_FRAMERATE,
};

/** @This returns the url
 *
 * @param upipe description structure of the pipe
 * @param url filled with current url
 * @return an error code
 */
static inline int upipe_qt_html_get_url(struct upipe *upipe,
                                                          const char **url)
{
    return upipe_control(upipe,UPIPE_QT_HTML_GET_URL,
                         UPIPE_QT_HTML_SIGNATURE, url);
}

/** @This returns the url
 *
 * @param upipe description structure of the pipe
 * @param url filled with current url
 * @return an error code
 */
static inline int upipe_qt_html_set_url(struct upipe *upipe,
                                                          const char *url)
{
    return upipe_control(upipe,UPIPE_QT_HTML_SET_URL,
                         UPIPE_QT_HTML_SIGNATURE, url);
}

/** @This sets the size of the pic.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal size
 * @param v vertical size
 * @return an error code
 */
static inline int upipe_qt_html_set_size(struct upipe *upipe,
                                                          int *h, int *v)
{
    return upipe_control(upipe, UPIPE_QT_HTML_SET_SIZE,
                          UPIPE_QT_HTML_SIGNATURE, h, v);
}

/** @This return the position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal position
 * @param v vertical position
 * @return an error code
 */
static inline int upipe_qt_html_get_size(struct upipe *upipe,
                                                         int h, int v)
{
    return upipe_control(upipe, UPIPE_QT_HTML_GET_SIZE,
                          UPIPE_QT_HTML_SIGNATURE, h, v);
}

/** @This sets the framerate of the pic.
 *
 * @param upipe description structure of the pipe
 * @param fr framerate
 * @return an error code
 */
static inline int upipe_qt_html_set_framerate(struct upipe *upipe,
                                                          int *fr)
{
    return upipe_control(upipe, UPIPE_QT_HTML_SET_SIZE,
                          UPIPE_QT_HTML_SIGNATURE, fr);
}

/** @This return the framerate of the pic.
 *
 * @param upipe description structure of the pipe
 * @param fr framerate
 * @return an error code
 */
static inline int upipe_qt_html_get_framerate(struct upipe *upipe,
                                                         int fr)
{
    return upipe_control(upipe, UPIPE_QT_HTML_GET_SIZE,
                          UPIPE_QT_HTML_SIGNATURE, fr);
}

#ifdef __cplusplus
}
#endif
#endif

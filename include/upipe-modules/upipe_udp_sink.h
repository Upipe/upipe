/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
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
 * @short Upipe sink module for udp
 */

#ifndef _UPIPE_MODULES_UPIPE_UDP_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_UDP_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_UDPSINK_SIGNATURE UBASE_FOURCC('u','s','n','k')

/** @This defines udp opening modes. */
enum upipe_udpsink_mode {
    /** do not do anything besides opening the fd */
    UPIPE_UDPSINK_NONE = 0,
};

/** @This extends upipe_command with specific commands for udp sink. */
enum upipe_udpsink_command {
    UPIPE_UDPSINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the uri of the currently opened udp (const char **) */
    UPIPE_UDPSINK_GET_URI,
    /** asks to open the given uri (const char *, enum upipe_udpsink_mode) */
    UPIPE_UDPSINK_SET_URI
};

/** @This returns the management structure for all udp sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsink_mgr_alloc(void);

/** @This returns the uri of the currently opened udp uri.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the udp
 * @return false in case of error
 */
static inline bool upipe_udpsink_get_uri(struct upipe *upipe,
                                        const char **uri_p)
{
    return upipe_control(upipe, UPIPE_UDPSINK_GET_URI, UPIPE_UDPSINK_SIGNATURE,
                         uri_p);
}

/** @This asks to open the given udp uri.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri
 * @param mode mode of opening the uri
 * @return false in case of error
 */
static inline bool upipe_udpsink_set_uri(struct upipe *upipe, const char *uri,
                                        enum upipe_udpsink_mode mode)
{
    return upipe_control(upipe, UPIPE_UDPSINK_SET_URI, UPIPE_UDPSINK_SIGNATURE,
                         uri, mode);
}

#ifdef __cplusplus
}
#endif
#endif

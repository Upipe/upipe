/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
            Benjamin Cohen
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
 * @short Upipe source module for udp sockets
 */

#ifndef _UPIPE_MODULES_UPIPE_UDP_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_UDP_SOURCE_H_

#include <upipe/upipe.h>

#define UPIPE_UDPSRC_SIGNATURE UBASE_FOURCC('u','s','r','c')

/** @This extends upipe_command with specific commands for udp socket source. */
enum upipe_udpsrc_command {
    UPIPE_UDPSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the path of the currently opened udp socket (const char **) */
    UPIPE_UDPSRC_GET_URI,
    /** asks to open the given path (const char *) */
    UPIPE_UDPSRC_SET_URI,
};

/** @This returns the management structure for all udp socket sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsrc_mgr_alloc(void);

/** @This returns the path of the currently opened udp socket.
 *
 * @param upipe description structure of the pipe
 * @param path_p filled in with the path of the udp socket
 * @return false in case of error
 */
static inline bool upipe_udpsrc_get_uri(struct upipe *upipe, const char **path_p)
{
    return upipe_control(upipe, UPIPE_UDPSRC_GET_URI, UPIPE_UDPSRC_SIGNATURE,
                         path_p);
}

/** @This asks to open the given udp socket.
 *
 * @param upipe description structure of the pipe
 * @param path relative or absolute path of the udp socket
 * @return false in case of error
 */
static inline bool upipe_udpsrc_set_uri(struct upipe *upipe, const char *path)
{
    return upipe_control(upipe, UPIPE_UDPSRC_SET_URI, UPIPE_UDPSRC_SIGNATURE,
                         path);
}

#endif

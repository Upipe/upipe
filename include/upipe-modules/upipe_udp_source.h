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
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_UDPSRC_SIGNATURE UBASE_FOURCC('u','s','r','c')

/** @This extends upipe_command with specific commands. */
enum upipe_udpsrc_command {
    UPIPE_UDPSRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get socket fd (int*) **/
    UPIPE_UDPSRC_GET_FD,
    /** set socket fd (int) **/
    UPIPE_UDPSRC_SET_FD,
};

/** @This extends uprobe_throw with specific events . */
enum uprobe_udpsrc_event {
    UPROBE_UDPSRC_SENTINEL = UPROBE_LOCAL,

    /** remote address changed (const struct sockaddr*, socklen_t) **/
    UPROBE_UDPSRC_NEW_PEER,
};

/** @This returns currently opened udp fd.
 *
 * @param upipe description structure of the pipe
 * @param fd_p filled in with the fd of the udp
 * @return false in case of error
 */
static inline int upipe_udpsrc_get_fd(struct upipe *upipe, int *fd_p)
{
    return upipe_control(upipe, UPIPE_UDPSRC_GET_FD, UPIPE_UDPSRC_SIGNATURE,
                         fd_p);
}

/** @This sets the udp fd.
 *
 * @param upipe description structure of the pipe
 * @param fd file descriptor
 * @return false in case of error
 */
static inline int upipe_udpsrc_set_fd(struct upipe *upipe, int fd)
{
    return upipe_control(upipe, UPIPE_UDPSRC_SET_FD, UPIPE_UDPSRC_SIGNATURE,
                         fd);
}

/** @This returns the management structure for all udp socket sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

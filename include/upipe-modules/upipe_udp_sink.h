/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
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

#include "upipe/upipe.h"
#include <sys/types.h>
#include <sys/socket.h>

#define UPIPE_UDPSINK_SIGNATURE UBASE_FOURCC('u','s','n','k')

/** @This extends upipe_command with specific commands for udp sink. */
enum upipe_udpsink_command {
    UPIPE_UDPSINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** get socket fd (int*) **/
    UPIPE_UDPSINK_GET_FD,
    /** set socket fd (int) **/
    UPIPE_UDPSINK_SET_FD,
    /** set remote address (const struct sockaddr *, socklen_t) **/
    UPIPE_UDPSINK_SET_PEER,
};

/** @This returns the management structure for all udp sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsink_mgr_alloc(void);

/** @This returns currently opened udp fd.
 *
 * @param upipe description structure of the pipe
 * @param fd_p filled in with the fd of the udp
 * @return false in case of error
 */
static inline int upipe_udpsink_get_fd(struct upipe *upipe, int *fd_p)
{
    return upipe_control(upipe, UPIPE_UDPSINK_GET_FD, UPIPE_UDPSINK_SIGNATURE,
                         fd_p);
}

/** @This sets the udp fd.
 *
 * @param upipe description structure of the pipe
 * @param fd file descriptor
 * @return false in case of error
 */
static inline int upipe_udpsink_set_fd(struct upipe *upipe, int fd)
{
    return upipe_control(upipe, UPIPE_UDPSINK_SET_FD, UPIPE_UDPSINK_SIGNATURE,
                         fd);
}

/** @This sets the remote address (for unconnected sockets).
 *
 * @param upipe description structure of the pipe
 * @param addr the remote address
 * @param addrlen the size of addr
 * @return false in case of error
 */
static inline int upipe_udpsink_set_peer(struct upipe *upipe,
        const struct sockaddr *addr, socklen_t addrlen)
{
    return upipe_control(upipe, UPIPE_UDPSINK_SET_PEER, UPIPE_UDPSINK_SIGNATURE,
            addr, addrlen);
}
#ifdef __cplusplus
}
#endif
#endif

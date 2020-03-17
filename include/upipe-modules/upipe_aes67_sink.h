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
 * @short Upipe sink module for aes67
 */

#ifndef _UPIPE_MODULES_UPIPE_AES67_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_AES67_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#include <sys/types.h>
#include <sys/socket.h>

#define UPIPE_AES67_SINK_SIGNATURE UBASE_FOURCC('a','6','7','k')

/** @This extends upipe_command with specific commands for aes67 sink. */
enum upipe_aes67_sink_command {
    UPIPE_AES67_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,
    /* const char*, const char* */
    UPIPE_AES67_SINK_OPEN_SOCKET,
    /* int, const char*, const char* */
    UPIPE_AES67_SINK_SET_FLOW_DESTINATION,
};

/** @This opens sockets and binds each to the interface given.
 *
 * @param upipe description structure of the pipe
 * @param path_1 first path interface to open and bind a socket.
 * @param path_2 second path interface. Can be NULL.
 * @return an error code
 */
static inline int upipe_aes67_sink_open_socket(struct upipe *upipe,
        const char *path_1, const char *path_2)
{
    return upipe_control(upipe, UPIPE_AES67_SINK_OPEN_SOCKET, UPIPE_AES67_SINK_SIGNATURE,
            path_1, path_2);
}

/** @This sets the destination details for the given flow.
 *
 * @param upipe description structure of the pipe
 * @param flow which flow this represents
 * @param path_1 destination IP and port for the first path.
 * @param path_2 destination IP and port for the second path. Can be NULL if
 * second path is not used.
 * @return an error code
 */
static inline int upipe_aes67_sink_set_flow_destination(struct upipe *upipe,
        int flow, const char *path_1, const char *path_2)
{
    return upipe_control(upipe, UPIPE_AES67_SINK_SET_FLOW_DESTINATION, UPIPE_AES67_SINK_SIGNATURE,
            flow, path_1, path_2);
}

/** @This returns the management structure for all aes67 sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_aes67_sink_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

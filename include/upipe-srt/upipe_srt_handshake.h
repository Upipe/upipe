/*
 * Copyright (C) 2023 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
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
 * @short Upipe module for SRT handshakes
 */

#ifndef _UPIPE_MODULES_UPIPE_SRT_HANDSHAKE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_SRT_HANDSHAKE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include <sys/types.h>
#include <sys/socket.h>

#define UPIPE_SRT_HANDSHAKE_SIGNATURE UBASE_FOURCC('s','r','t','h')
#define UPIPE_SRT_HANDSHAKE_OUTPUT_SIGNATURE UBASE_FOURCC('s','r','h','o')

/** @This extends upipe_command with specific commands for srt handshake. */
enum upipe_srt_handshake_command {
    UPIPE_SRT_HANDSHAKE_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set our peer address (const struct sockaddr *, socklen_t) **/
    UPIPE_SRT_HANDSHAKE_SET_PEER,

    /** set the encryption password (const char *) */
    UPIPE_SRT_HANDSHAKE_SET_PASSWORD,
};

/** @This sets the peer address
 *
 * @param upipe description structure of the pipe
 * @param addr our address
 * @param addrlen the size of addr
 * @return false in case of error
 */
static inline int upipe_srt_handshake_set_peer(struct upipe *upipe,
        const struct sockaddr *addr, socklen_t addrlen)
{
    return upipe_control(upipe, UPIPE_SRT_HANDSHAKE_SET_PEER, UPIPE_SRT_HANDSHAKE_SIGNATURE,
            addr, addrlen);
}

/** @This sets the encryption key
 *
 * @param upipe description structure of the pipe
 * @param even key
 * @param odd key
 * @return false in case of error
 */
static inline int upipe_srt_handshake_set_password(struct upipe *upipe,
        const char *password)
{
    return upipe_control(upipe, UPIPE_SRT_HANDSHAKE_SET_PASSWORD, UPIPE_SRT_HANDSHAKE_SIGNATURE,
            password);
}

/** @This returns the management structure for all srt handshakes sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_srt_handshake_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

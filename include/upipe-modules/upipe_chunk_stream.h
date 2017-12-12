/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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
 * @short Upipe chunk module - outputs fixed-length blocks from stream
 */

#ifndef _UPIPE_MODULES_UPIPE_CHUNK_STREAM_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_CHUNK_STREAM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>

#define UPIPE_CHUNK_STREAM_SIGNATURE UBASE_FOURCC('c','h','u','n')

/** @This extends upipe_command with specific commands for chunk_stream pipes. */
enum upipe_chunk_stream_command {
    UPIPE_CHUNK_STREAM_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set chunk size (unsigned int, unsigned int) */
    UPIPE_CHUNK_STREAM_SET_MTU,
    /** set chunk size (unsigned int*, unsigned int*) */
    UPIPE_CHUNK_STREAM_GET_MTU,
};

/** @This returns the configured mtu of TS packets.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @param align_p filled in with the configured alignment, in octets
 * @return an error code
 */
static inline int upipe_chunk_stream_get_mtu(struct upipe *upipe,
                               unsigned int *mtu_p, unsigned int *align_p)
{
    return upipe_control(upipe, UPIPE_CHUNK_STREAM_GET_MTU,
                         UPIPE_CHUNK_STREAM_SIGNATURE, mtu_p, align_p);
}

/** @This sets the configured mtu of TS packets.
 * @param upipe description structure of the pipe
 * @param mtu max packet size, in octets
 * @param align packet chunk alignment, in octets
 * @return an error code
 */
static inline int upipe_chunk_stream_set_mtu(struct upipe *upipe,
                               unsigned int mtu, unsigned int align)
{
    return upipe_control(upipe, UPIPE_CHUNK_STREAM_SET_MTU,
                         UPIPE_CHUNK_STREAM_SIGNATURE, mtu, align);
}

/** @This returns the management structure for chunk_stream pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_chunk_stream_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short Upipe module - generates auxiliary blocks from k.systime
 * This module outputs an urefblock containing the (network-endian)
 * k.systime value from the input uref.
 * This is typically used as an input for fsink (or any fsink-like
 * pipe) to store multicat auxiliary files.
 */

#ifndef _UPIPE_MODULES_UPIPE_GENAUX_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_GENAUX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <upipe/upipe.h>
#include <upipe/uref_block.h>

#define UPIPE_GENAUX_SIGNATURE UBASE_FOURCC('g','a','u','x')

/** @This extends upipe_command with specific commands for genaux pipes. */
enum upipe_genaux_command {
    UPIPE_GENAUX_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set getter (int (*)(struct uref*, uint64_t*)) */
    UPIPE_GENAUX_SET_GETATTR,

    /** get getter (int (*)(struct uref*, uint64_t*)) */
    UPIPE_GENAUX_GET_GETATTR,
};

/** @This sets the get callback to fetch the u64 opaque with.
 *
 * @param upipe description structure of the pipe
 * @param get callback
 * @return an error code
 */
static inline int upipe_genaux_set_getattr(struct upipe *upipe,
                            int (*get)(struct uref*, uint64_t*))
{
    return upipe_control(upipe, UPIPE_GENAUX_SET_GETATTR,
                         UPIPE_GENAUX_SIGNATURE, get);
}

/** @This gets the get callback to fetch the u64 opaque with.
 *
 * @param upipe description structure of the pipe
 * @param get callback pointer
 * @return an error code
 */
static inline int upipe_genaux_get_getattr(struct upipe *upipe,
                            int (**get)(struct uref*, uint64_t*))
{
    return upipe_control(upipe, UPIPE_GENAUX_GET_GETATTR,
                         UPIPE_GENAUX_SIGNATURE, get);
}

/** @This returns the management structure for genaux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_genaux_mgr_alloc(void);

/** @This swaps uint64 to net-endian
 *
 * @param buf destination buffer
 * @param opaque uint64 opaque to swap
 */
static inline void upipe_genaux_hton64(uint8_t *buf, uint64_t opaque)
{
    buf[0] = opaque >> 56;
    buf[1] = (opaque >> 48) & 0xff;
    buf[2] = (opaque >> 40) & 0xff;
    buf[3] = (opaque >> 32) & 0xff;
    buf[4] = (opaque >> 24) & 0xff;
    buf[5] = (opaque >> 16) & 0xff;
    buf[6] = (opaque >> 8) & 0xff;
    buf[7] = (opaque >> 0) & 0xff;
}

/** @This swaps uint64 from net-endian
 *
 * @param buf source buffer
 * @return uint64 host-endian
 */
static inline uint64_t upipe_genaux_ntoh64(const uint8_t *buf)
{
    return ((uint64_t)buf[0] << 56)
         | ((uint64_t)buf[1] << 48)
         | ((uint64_t)buf[2] << 40)
         | ((uint64_t)buf[3] << 32)
         | ((uint64_t)buf[4] << 24)
         | ((uint64_t)buf[5] << 16)
         | ((uint64_t)buf[6] << 8)
         | ((uint64_t)buf[7] << 0);
}

#ifdef __cplusplus
}
#endif
#endif

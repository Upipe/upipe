/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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

#ifndef _UPIPE_MODULES_UPIPE_BLIT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_BLIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>
#define UPIPE_BLIT_SIGNATURE UBASE_FOURCC('b','l','i','t')
#define UPIPE_BLIT_INPUT_SIGNATURE UBASE_FOURCC('b','l','i','s')

/** @This returns the management structure for blit pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_blit_mgr_alloc(void);

/** @This extends upipe_command with specific commands for upipe_blit_sub pipes.
 */
enum upipe_blit_sub_command {
    UPIPE_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_SUB_SET_POSITIONH,

    UPIPE_SUB_GET_POSITIONH,

    UPIPE_SUB_SET_POSITIONV,

    UPIPE_SUB_GET_POSITIONV,

    UPIPE_SUB_SET_POSITION,

    UPIPE_SUB_GET_POSITION,
};

/** @This returns the current H position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param h filled with current horizontal position
 * @return an error code
 */
static inline enum ubase_err upipe_blit_sub_get_hposition(struct upipe *upipe,
                                                           int *h)
{
    return upipe_control(upipe,UPIPE_SUB_GET_POSITIONH,
                         UPIPE_BLIT_INPUT_SIGNATURE, h);
}

/** @This sets the H position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal position
 * @return an error code
 */
static inline enum ubase_err upipe_blit_sub_set_hposition(struct upipe *upipe,
                                                       int h)
{
    return upipe_control(upipe, UPIPE_SUB_SET_POSITIONH,
                         UPIPE_BLIT_INPUT_SIGNATURE, h);
}

/** @This returns the current V position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param v filled with current vertical position
 * @return an error code
 */
static inline enum ubase_err upipe_blit_sub_get_vposition(struct upipe *upipe,
                                                           int *v)
{
    return upipe_control(upipe,UPIPE_SUB_GET_POSITIONV,
                         UPIPE_BLIT_INPUT_SIGNATURE, v);
}

/** @This sets the V position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param v vertical position
 * @return an error code
 */
static inline enum ubase_err upipe_blit_sub_set_vposition(struct upipe *upipe,
                                                       int v)
{
    return upipe_control(upipe, UPIPE_SUB_SET_POSITIONV,
                         UPIPE_BLIT_INPUT_SIGNATURE, v);
}

/** @This sets the position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal position
 * @param v vertical position
 * @return an error code
 */
static inline enum ubase_err upipe_blit_sub_set_position(struct upipe *upipe,
                                                          int *h, int *v)
{
    return upipe_control(upipe, UPIPE_SUB_SET_POSITION,
                          UPIPE_BLIT_INPUT_SIGNATURE, h, v);
}

/** @This return the position of the sub.
 *
 * @param upipe description structure of the pipe
 * @param h horizontal position
 * @param v vertical position
 * @return an error code
 */
static inline enum ubase_err upipe_blit_sub_get_position(struct upipe *upipe,
                                                          int h, int v)
{
    return upipe_control(upipe, UPIPE_SUB_GET_POSITION,
                          UPIPE_BLIT_INPUT_SIGNATURE, h, v);
}
#ifdef __cplusplus
}
#endif
#endif

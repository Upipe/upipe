/*****************************************************************************
 * upipe_sws_thumbs.h: application interface for sws module
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
 *
 * SPDX-License-Identifier: MIT
 *****************************************************************************/

/** @file
 * @short Upipe swscale thumbnail gallery module
 */

#ifndef _UPIPE_SWSCALE_UPIPE_SWS_THUMBS_H_
/** @hidden */
#define _UPIPE_SWSCALE_UPIPE_SWS_THUMBS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SWS_THUMBS_SIGNATURE UBASE_FOURCC('s','w','s','t')

/** @This extends upipe_command with specific commands for avcodec decode. */
enum upipe_sws_thumbs_command {
    UPIPE_SWS_THUMBS_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set size (int, int, int, int) */
    UPIPE_SWS_THUMBS_SET_SIZE,
    /** get size (int*, int*, int*, int*) */
    UPIPE_SWS_THUMBS_GET_SIZE,
    /** flush before next uref */
    UPIPE_SWS_THUMBS_FLUSH_NEXT
};

/** @This sets the thumbnail gallery dimensions.
 *
 * @param upipe description structure of the pipe
 * @param size size parameter (0=disabled)
 * @return false in case of error
 */
static inline bool upipe_sws_thumbs_set_size(struct upipe *upipe,
                                      int hsize, int vsize, int cols, int rows)
{
    return upipe_control(upipe, UPIPE_SWS_THUMBS_SET_SIZE, UPIPE_SWS_THUMBS_SIGNATURE,
                         hsize, vsize, cols, rows);
}

/** @This gets the thumbnail gallery dimensions.
 *
 * @param upipe description structure of the pipe
 * @param size size parameter (0=disabled)
 * @return false in case of error
 */
static inline bool upipe_sws_thumbs_get_size(struct upipe *upipe,
                          int *hsize_p, int *vsize_p, int *cols_p, int *rows_p)
{
    return upipe_control(upipe, UPIPE_SWS_THUMBS_GET_SIZE, UPIPE_SWS_THUMBS_SIGNATURE,
                         hsize_p, vsize_p, cols_p, rows_p);
}

/** @This flushes the current gallery before next uref
 *
 * @param upipe description structure of the pipe
 * @return false in case of error
 */
static inline bool upipe_sws_thumbs_flush_next(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_SWS_THUMBS_FLUSH_NEXT, UPIPE_SWS_THUMBS_SIGNATURE);
}

/** @This returns the management structure for sws thmub pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_sws_thumbs_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

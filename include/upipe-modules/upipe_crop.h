/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module cropping incoming pictures
 */

#ifndef _UPIPE_MODULES_UPIPE_CROP_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_CROP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_CROP_SIGNATURE UBASE_FOURCC('c','r','o','p')

/** @This returns the management structure for crop pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_crop_mgr_alloc(void);

/** @This extends upipe_command with specific commands for upipe_crop pipes.
 */
enum upipe_crop_command {
    UPIPE_CROP_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** gets the offsets of the cropped rect
     * (int64_t *, int64_t *, int64_t *, int64_t *) */
    UPIPE_CROP_GET_RECT,
    /** sets the offsets of the cropped rect
     * (int64_t, int64_t, int64_t, int64_t) */
    UPIPE_CROP_SET_RECT
};

/** @This gets the offsets (from the respective borders of the frame) of the
 * cropped rectangle.
 *
 * @param upipe description structure of the pipe
 * @param loffset_p filled in with the offset from the left border
 * @param roffset_p filled in with the offset from the right border
 * @param toffset_p filled in with the offset from the top border
 * @param boffset_p filled in with the offset from the bottom border
 * @return an error code
 */
static inline int upipe_crop_get_rect(struct upipe *upipe,
        int64_t *loffset_p, int64_t *roffset_p,
        int64_t *toffset_p, int64_t *boffset_p)
{
    return upipe_control(upipe, UPIPE_CROP_GET_RECT,
                         UPIPE_CROP_SIGNATURE,
                         loffset_p, roffset_p, toffset_p, boffset_p);
}

/** @This sets the offsets (from the respective borders of the frame) of the
 * cropped rectangle.
 *
 * @param upipe description structure of the pipe
 * @param loffset offset from the left border
 * @param roffset offset from the right border
 * @param toffset offset from the top border
 * @param boffset offset from the bottom border
 * @return an error code
 */
static inline int upipe_crop_set_rect(struct upipe *upipe,
        int64_t loffset, int64_t roffset, int64_t toffset, int64_t boffset)
{
    return upipe_control(upipe, UPIPE_CROP_SET_RECT,
                         UPIPE_CROP_SIGNATURE,
                         loffset, roffset, toffset, boffset);
}

#ifdef __cplusplus
}
#endif
#endif

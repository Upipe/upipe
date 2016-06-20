/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Sebastien Gougelet
 *          Christophe Massiot
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
 * @short Upipe module blitting subpictures into a main picture
 */

#ifndef _UPIPE_MODULES_UPIPE_BLIT_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_BLIT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_BLIT_SIGNATURE UBASE_FOURCC('b','l','i','t')
#define UPIPE_BLIT_SUB_SIGNATURE UBASE_FOURCC('b','l','i','s')

/** @This returns the management structure for blit pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_blit_mgr_alloc(void);

/** @This extends upipe_command with specific commands for upipe_blit_sub pipes.
 */
enum upipe_blit_sub_command {
    UPIPE_BLIT_SUB_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** gets the offsets of the rect onto which the input of this subpipe
     * will be blitted (uint64_t, uint64_t, uint64_t, uint64_t) */
    UPIPE_BLIT_SUB_GET_RECT,
    /** sets the offsets of the rect onto which the input of this subpipe
     * will be blitted (uint64_t, uint64_t, uint64_t, uint64_t) */
    UPIPE_BLIT_SUB_SET_RECT,
    /** sets the method for alpha blending (uint8_t)
     * @see ubuf_pic_blit */
    UPIPE_BLIT_SUB_SET_ALPHA_THRESHOLD,
};

/** @This sets the method for alpha blending for this subpipe.
 *
 * @param upipe description structure of the pipe
 * @param threshold method for alpha blending (@see ubuf_pic_blit)
 * @return an error code
 */
static inline int upipe_blit_sub_set_alpha_threshold(struct upipe *upipe,
        uint8_t threshold)
{
    return upipe_control(upipe, UPIPE_BLIT_SUB_SET_ALPHA_THRESHOLD,
                         UPIPE_BLIT_SUB_SIGNATURE, threshold);
}

/** @This gets the offsets (from the respective borders of the frame) of the
 * rectangle onto which the input of the subpipe will be blitted.
 *
 * @param upipe description structure of the pipe
 * @param loffset_p filled in with the offset from the left border
 * @param roffset_p filled in with the offset from the right border
 * @param toffset_p filled in with the offset from the top border
 * @param boffset_p filled in with the offset from the bottom border
 * @return an error code
 */
static inline int upipe_blit_sub_get_rect(struct upipe *upipe,
        uint64_t *loffset_p, uint64_t *roffset_p,
        uint64_t *toffset_p, uint64_t *boffset_p)
{
    return upipe_control(upipe, UPIPE_BLIT_SUB_GET_RECT,
                         UPIPE_BLIT_SUB_SIGNATURE,
                         loffset_p, roffset_p, toffset_p, boffset_p);
}

/** @This sets the offsets (from the respective borders of the frame) of the
 * rectangle onto which the input of the subpipe will be blitted.
 *
 * @param upipe description structure of the pipe
 * @param loffset offset from the left border
 * @param roffset offset from the right border
 * @param toffset offset from the top border
 * @param boffset offset from the bottom border
 * @return an error code
 */
static inline int upipe_blit_sub_set_rect(struct upipe *upipe,
        uint64_t loffset, uint64_t roffset, uint64_t toffset, uint64_t boffset)
{
    return upipe_control(upipe, UPIPE_BLIT_SUB_SET_RECT,
                         UPIPE_BLIT_SUB_SIGNATURE,
                         loffset, roffset, toffset, boffset);
}

#ifdef __cplusplus
}
#endif
#endif

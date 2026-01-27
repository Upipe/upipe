/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe ubuf manager for picture formats with blackmagic storage
 */

#ifndef _UPIPE_BLACKMAGIC_UBUF_PIC_BLACKMAGIC_H_
/** @hidden */
#define _UPIPE_BLACKMAGIC_UBUF_PIC_BLACKMAGIC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_pic.h"

#include <stdint.h>
#include <stdbool.h>

/** @This is a simple signature to make sure the ubuf_alloc internal API
 * is used properly. */
#define UBUF_BMD_ALLOC_PICTURE UBASE_FOURCC('b','m','d','p')

/** @This extends ubuf_command with specific commands for blackmagic picture
 * allocator. */
enum ubuf_pic_bmd_command {
    UBUF_PIC_BMD_SENTINEL = UBUF_CONTROL_LOCAL,

    /** returns the blackmagic video frame (void **) */
    UBUF_PIC_BMD_GET_VIDEO_FRAME
};

/** @This returns a new ubuf from a blackmagic picture allocator.
 *
 * @param mgr management structure for this ubuf type
 * @param VideoFrame pointer to IDeckLinkVideoFrame
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_pic_bmd_alloc(struct ubuf_mgr *mgr,
                                              void *VideoFrame)
{
    return ubuf_alloc(mgr, UBUF_BMD_ALLOC_PICTURE, VideoFrame);
}

/** @This returns the blackmagic video frame. The reference counter is not
 * incremented.
 *
 * @param ubuf pointer to ubuf
 * @param VideoFrame_p filled in with a pointer to IDeckLinkVideoFrame
 * @return an error code
 */
static inline int ubuf_pic_bmd_get_video_frame(struct ubuf *ubuf,
                                               void **VideoFrame_p)
{
    return ubuf_control(ubuf, UBUF_PIC_BMD_GET_VIDEO_FRAME,
                        UBUF_BMD_ALLOC_PICTURE, VideoFrame_p);
}

/** @This allocates a new instance of the ubuf manager for picture formats
 * using blackmagic.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param PixelFormat blackmagic pixel format
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_pic_bmd_mgr_alloc(uint16_t ubuf_pool_depth,
                                        uint32_t PixelFormat);

#ifdef __cplusplus
}
#endif
#endif

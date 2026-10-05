/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe DVB TTML subtitle decoder
 */

#ifndef _UPIPE_TTML_UPIPE_TTML_DEC_H_
/** @hidden */
#define _UPIPE_TTML_UPIPE_TTML_DEC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TTMLD_SIGNATURE UBASE_FOURCC('t','t','m','l')

/** @This enumerates the ttmld pipe commands. */
enum upipe_ttmld_command {
    /** sentinel */
    UPIPE_TTMLD_SENTINEL = UPIPE_CONTROL_LOCAL,
    /** set the size of the canvas the regions are positioned on
     * (uint64_t, uint64_t) */
    UPIPE_TTMLD_SET_CANVAS_SIZE,
};

/** @This sets the size of the canvas the regions are positioned on.
 *
 * The TTML root container is mapped onto it, so it is the size of the picture
 * the subtitles are composed with.  It has to be set before the first
 * document, and set again whenever that picture changes size: the pipe has no
 * default, because a canvas of the wrong size puts every region in the wrong
 * place rather than merely rendering them at the wrong resolution.
 *
 * @param upipe description structure of the pipe
 * @param hsize width of the canvas
 * @param vsize height of the canvas
 * @return an error code
 */
static inline int upipe_ttmld_set_canvas_size(struct upipe *upipe,
                                              uint64_t hsize, uint64_t vsize)
{
    return upipe_control(upipe, UPIPE_TTMLD_SET_CANVAS_SIZE,
                         UPIPE_TTMLD_SIGNATURE, hsize, vsize);
}

/** @This returns the management structure for all ttmld pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ttmld_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

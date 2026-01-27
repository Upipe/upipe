/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré <funman@videolan.org>
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe speexdsp resampler module
 */

#ifndef _UPIPE_SPEEXDSP_UPIPE_SPEEXDSP_H_
/** @hidden */
#define _UPIPE_SPEEXDSP_UPIPE_SPEEXDSP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_SPEEXDSP_SIGNATURE UBASE_FOURCC('s','p','x','d')

enum upipe_speexdsp_command {
    UPIPE_SPEEXDSP_SENTINAL = UPIPE_CONTROL_LOCAL,

    UPIPE_SPEEXDSP_RESET_RESAMPLER, /* int sig */
};

static inline int upipe_speexdsp_reset_resampler(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_SPEEXDSP_RESET_RESAMPLER, UPIPE_SPEEXDSP_SIGNATURE);
}

/** @This returns the management structure for speexdsp pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_speexdsp_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_HLS_UPIPE_HLS_VARIANT_H_
# define _UPIPE_HLS_UPIPE_HLS_VARIANT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"
#include "upipe/upipe.h"

#define UPIPE_HLS_VARIANT_SIGNATURE     UBASE_FOURCC('h','l','s','V')
#define UPIPE_HLS_VARIANT_SUB_SIGNATURE UBASE_FOURCC('h','l','s','v')

enum upipe_hls_variant_command {
    UPIPE_HLS_VARIANT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** play a variant (struct uref *) */
    UPIPE_HLS_VARIANT_PLAY,
};

static inline const char *upipe_hls_variant_command_str(int cmd)
{
    switch ((enum upipe_hls_variant_command)cmd) {
    UBASE_CASE_TO_STR(UPIPE_HLS_VARIANT_PLAY);
    case UPIPE_HLS_VARIANT_SENTINEL: break;
    }
    return NULL;
}

static inline int upipe_hls_variant_play(struct upipe *upipe,
                                         struct uref *variant)
{
    return upipe_control(upipe, UPIPE_HLS_VARIANT_PLAY,
                         UPIPE_HLS_VARIANT_SIGNATURE, variant);
}

struct upipe_mgr *upipe_hls_variant_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

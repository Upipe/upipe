/*****************************************************************************
 * upipe_av.h: application interface for common function for libav wrappers
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * SPDX-License-Identifier: MIT
 *****************************************************************************/

#ifndef _UPIPE_AV_UPIPE_AV_H_
/** @hidden */
#define _UPIPE_AV_UPIPE_AV_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** @hidden */
struct uprobe;

/** @This initializes non-reentrant parts of avcodec and avformat. Call it
 * before allocating managers from this library.
 *
 * @param init_avcodec_only if set to true, avformat source and sink may not
 * be used (saves memory)
 * @param uprobe uprobe to print libav messages
 * @return false in case of error
 */
bool upipe_av_init(bool init_avcodec_only, struct uprobe *uprobe);

/** @This cleans up memory allocated by @ref upipe_av_init. Call it when all
 * avformat- and avcodec-related managers have been freed.
 */
void upipe_av_clean(void);

#ifdef __cplusplus
}
#endif
#endif

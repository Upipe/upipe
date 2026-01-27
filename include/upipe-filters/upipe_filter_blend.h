/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe blend deinterlace filter
 */

#ifndef _UPIPE_FILTERS_UPIPE_FILTER_BLEND_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_FILTER_BLEND_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_FILTER_BLEND_SIGNATURE UBASE_FOURCC('b', 'l', 'e', 'n')

/** @This returns the management structure for all avformat sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_filter_blend_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

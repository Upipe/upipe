/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe MacOSX AudioQueue sink module.
 */

#ifndef _UPIPE_OSX_UPIPE_OSX_AUDIOQUEUE_SINK_H_
/** @hidden */
#define _UPIPE_OSX_UPIPE_OSX_AUDIOQUEUE_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_OSX_AUDIOQUEUE_SINK_SIGNATURE UBASE_FOURCC('o', 's', 'x', 'a')

/** @This returns the management structure for all osx_audioqueue sinks.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_osx_audioqueue_sink_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module creating timestamps for single streams
 *
 * This module is used for simple pipelines where only a single elementary
 * stream is played, and no demux is used. As there is no demux the packets
 * do not get any timestamp. This module allows to create timestamps so that
 * the stream can be played.
 */

#ifndef _UPIPE_MODULES_UPIPE_NODEMUX_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NODEMUX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_NODEMUX_SIGNATURE UBASE_FOURCC('n','d','m','x')

/** @This returns the management structure for all nodemux pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_nodemux_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

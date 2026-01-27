/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe linear module prepending 5 lines to compressed NTSC video
 *        and flagging as Bottom-Field-First.
 */

#ifndef _UPIPE_MODULES_UPIPE_NTSC_PREPEND_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NTSC_PREPEND_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/upipe.h"

#define UPIPE_NTSC_PREPEND_SIGNATURE UBASE_FOURCC('n','t','s','p')

/** @This returns the management structure for ntsc_prepend pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ntsc_prepend_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

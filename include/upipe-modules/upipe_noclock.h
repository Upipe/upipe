/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module creating system timestamps for off-line streams
 *
 * This module is used for pipelines running off-line (for instance to transcode
 * a file to a file). In that case there is no system timestamp in input
 * packets, but some sinks (multiplexers) require system timestamps. This
 * module copies the program timestamp into the system timestamp. Please note
 * that this will only work if all flows use the same program clock, that is
 * if there is only one program involved.
 */

#ifndef _UPIPE_MODULES_UPIPE_NOCLOCK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NOCLOCK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_NOCLOCK_SIGNATURE UBASE_FOURCC('n','c','l','k')

/** @This returns the management structure for all noclock pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_noclock_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

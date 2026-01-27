/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module adding SMPTE 337 encapsulation
 */

#ifndef _UPIPE_MODULES_UPIPE_S337_ENCAPS_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_S337_ENCAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_S337E_SIGNATURE UBASE_FOURCC('3','3','7','e')

/** @This returns the management structure for s337e pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s337_encaps_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

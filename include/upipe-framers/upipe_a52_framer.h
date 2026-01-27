/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from a ATSC A/52:2012 stream
 * This framer supports A/52:2012 and A/52:2012 Annex E streams.
 */

#ifndef _UPIPE_FRAMERS_UPIPE_A52_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_A52_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_A52F_SIGNATURE UBASE_FOURCC('a','5','2','f')

/** @This returns the management structure for all a52f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_a52f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

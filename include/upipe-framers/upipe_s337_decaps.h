/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of a SMPTE 337 stream
 * This pipe only supports the 16-bit mode.
 *
 * Normative references:
 *  - SMPTE 337-2008 (non-PCM in AES3)
 *  - SMPTE 338-2008 (non-PCM in AES3 - data types)
 *  - SMPTE 340-2008 (non-PCM in AES3 - ATSC A/52B)
 */

#ifndef _UPIPE_FRAMERS_UPIPE_S337_DECAPS_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_S337_DECAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_S337D_SIGNATURE UBASE_FOURCC('3','3','7','d')

/** @This returns the management structure for all s337d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s337d_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

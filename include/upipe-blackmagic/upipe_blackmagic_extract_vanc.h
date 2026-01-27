/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
*/

/** @file
 * @short Upipe module to extract Blackmagic vertical ancillary data
 */

#ifndef _UPIPE_BLACKMAGIC_UPIPE_BLACKMAGIC_EXTRACT_VANC_H_
/** @hidden */
#define _UPIPE_BLACKMAGIC_UPIPE_BLACKMAGIC_EXTRACT_VANC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_BMD_VANC_SIGNATURE UBASE_FOURCC('b', 'm', 'd', 'v')

/** @This returns the management structure for all bmd_vanc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_vanc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

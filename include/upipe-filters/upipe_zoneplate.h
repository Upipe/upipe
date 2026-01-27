/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module drawing zoneplate video pictures
 */

#ifndef _UPIPE_FILTERS_UPIPE_ZONEPLATE_H_
#define _UPIPE_FILTERS_UPIPE_ZONEPLATE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"

#define UPIPE_ZP_SIGNATURE   UBASE_FOURCC('z','p','l','t')

/** @This returns the management structure for zoneplate pipes.
 *
 * @return a pointer to the zoneplate pipe manager
 */
struct upipe_mgr *upipe_zp_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

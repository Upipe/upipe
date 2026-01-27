/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe linear module sending output identical to input
 */

#ifndef _UPIPE_MODULES_UPIPE_IDEM_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_IDEM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/upipe.h"

#define UPIPE_IDEM_SIGNATURE UBASE_FOURCC('i','d','e','m')

/** @This returns the management structure for idem pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_idem_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

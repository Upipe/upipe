/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module evening the start and end of a stream
 */

#ifndef _UPIPE_MODULES_UPIPE_EVEN_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_EVEN_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_EVEN_SIGNATURE UBASE_FOURCC('e','v','e','n')
#define UPIPE_EVEN_SUB_SIGNATURE UBASE_FOURCC('e','v','e','s')

/** @This returns the management structure for all even pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_even_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

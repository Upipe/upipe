/*
 * Copyright (C) 2016-2017 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 */

#ifndef _UPIPE_FRAMERS_UPIPE_S337_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_S337_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_S337F_SIGNATURE UBASE_FOURCC('s','3','3','7')

/** @This returns the management structure for all s337f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_s337f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

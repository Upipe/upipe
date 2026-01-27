/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe zvbi encoding module
 */

#ifndef _UPIPE_ZVBI_UPIPE_ZVBIENC_H_
/** @hidden */
#define _UPIPE_ZVBI_UPIPE_ZVBIENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_ZVBIENC_SIGNATURE UBASE_FOURCC('z','v','b','e')

/** @This returns the management structure for zvbi pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_zvbienc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2017 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module generating zoneplate video pictures
 */

#ifndef _UPIPE_FILTERS_UPIPE_ZONEPLATE_SOURCE_H_
#define _UPIPE_FILTERS_UPIPE_ZONEPLATE_SOURCE_H_

#include "upipe/ubase.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @This is the signature of a zoneplate source pipe. */
#define UPIPE_ZPSRC_SIGNATURE    UBASE_FOURCC('z','p','s','r')

/** @This returns the zoneplate source pipe manager.
 *
 * @return a pointer to the zoneplate source pipe manager
 */
struct upipe_mgr *upipe_zpsrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_FILTERS_UPIPE_ZONEPLATE_SOURCE_H_ */

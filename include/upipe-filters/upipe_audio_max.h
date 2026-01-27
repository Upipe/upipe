/*
 * Copyright (C) 2016 Open Broadcast System Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe filter computing the maximum amplitude per uref
 */

#ifndef _UPIPE_FILTERS_UPIPE_AUDIO_MAX_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_AUDIO_MAX_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"
#include "upipe/uref_attr.h"
#include <stdint.h>

UREF_ATTR_FLOAT_VA(amax, amplitude, "amax.amp[%" PRIu8"]", max amplitude,
        uint8_t plane, plane)

#define UPIPE_AUDIO_MAX_SIGNATURE UBASE_FOURCC('a', 'm', 'a', 'x')

/** @This returns the management structure for all amax sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_amax_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

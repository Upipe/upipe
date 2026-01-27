/*
 * Copyright (C) 2016 Open Broadcast Systems Ltd
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Rafaël Carré
 *          Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module generating audio graphs pictures
 */

#ifndef _UPIPE_FILTERS_UPIPE_AUDIO_GRAPH_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_AUDIO_GRAPH_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_AUDIO_GRAPH_SIGNATURE UBASE_FOURCC('a','g','p','h')

/** @This returns the management structure for agraph pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_agraph_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

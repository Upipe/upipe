/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of a DVB subtitles stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_DVBSUB_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_DVBSUB_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_DVBSUBF_SIGNATURE UBASE_FOURCC('d','s','b','f')
/** We only accept the EN 300 743 elementary stream. */
#define UPIPE_DVBSUBF_EXPECTED_FLOW_DEF "block.dvb_subtitle."

/** @This returns the management structure for all dvbsubf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_dvbsubf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of a DVB teletext stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_TELX_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_TELX_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TELXF_SIGNATURE UBASE_FOURCC('t','l','x','f')
/** We only accept the EN 300 706 elementary stream. */
#define UPIPE_TELXF_EXPECTED_FLOW_DEF "block.dvb_teletext."

/** @This returns the management structure for all telxf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_telxf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of an ISO 13818-2 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_MPGV_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_MPGV_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_MPGVF_SIGNATURE UBASE_FOURCC('m','p','v','f')
/** We only accept the ISO 13818-2 elementary stream. */
#define UPIPE_MPGVF_EXPECTED_FLOW_DEF "block.mpeg2video."

/** @This returns the management structure for all mpgvf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mpgvf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

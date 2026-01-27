/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from an ISO 13818-3 or 7 stream
 * This framer supports levels 1, 2, 3 of ISO/IEC 11179-3 and ISO/IEC 13818-3,
 * and ISO/IEC 13818-7 (ADTS AAC) and ISO/IEC 14496-3 (raw AAC) streams
 */

#ifndef _UPIPE_FRAMERS_UPIPE_MPGA_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_MPGA_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_MPGAF_SIGNATURE UBASE_FOURCC('m','p','a','f')

/** @This returns the management structure for all mpgaf pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_mpgaf_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

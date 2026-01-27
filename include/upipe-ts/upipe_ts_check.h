/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module checking that a buffer contains a given number of aligned TS packets
 *
 * This module also accepts @ref upipe_set_output_size, with the following
 * common values:
 * @table 2
 * @item size (in octets) @item description
 * @item 188 @item standard size of TS packets according to ISO/IEC 13818-1
 * @item 196 @item TS packet followed by an 8-octet timestamp or checksum
 * @item 204 @item TS packet followed by a 16-octet checksum
 * @end table
 */

#ifndef _UPIPE_TS_UPIPE_TS_CHECK_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_TS_CHECK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_TS_CHECK_SIGNATURE UBASE_FOURCC('t','s','c','k')

/** @This returns the management structure for all ts_check pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_check_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

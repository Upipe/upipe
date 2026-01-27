/*
 * Copyright (C) 2023 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module building frames from chunks of an ID3v2 stream
 */

#ifndef _UPIPE_FRAMERS_UPIPE_ID3V2_FRAMER_H_
/** @hidden */
#define _UPIPE_FRAMERS_UPIPE_ID3V2_FRAMER_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_ID3V2F_SIGNATURE UBASE_FOURCC('I','D','3','f')
/** We only accept the ID3 elementary stream. */
#define UPIPE_ID3V2F_EXPECTED_FLOW_DEF "block.id3."

/** @This returns the management structure for all id3v2f pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_id3v2f_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

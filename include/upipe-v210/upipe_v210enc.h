/*
 * V210 encoder
 *
 * Copyright (C) 2009 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 * Copyright (c) 2015 Open Broadcast Systems Ltd
 *
 * This file is based on the implementation in FFmpeg.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe v210enc module
 */

#ifndef _UPIPE_V210_UPIPE_V210ENC_H_
/** @hidden */
#define _UPIPE_V210_UPIPE_V210ENC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_V210ENC_SIGNATURE UBASE_FOURCC('v','2','1','e')

/** @This returns the management structure for v210 pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_v210enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

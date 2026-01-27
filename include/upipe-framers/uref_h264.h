/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe h264 attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_H264_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_H264_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(h264, type, "h264.type", slice coding type)

#ifdef __cplusplus
}
#endif
#endif

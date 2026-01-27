/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe h264 flow definition attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_H264_FLOW_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_H264_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(h264_flow, profile, "h264.profile", profile)
UREF_ATTR_SMALL_UNSIGNED(h264_flow, profile_compatibility, "h264.profilecomp",
        profile compatibility)
UREF_ATTR_SMALL_UNSIGNED(h264_flow, level, "h264.level", level)

#ifdef __cplusplus
}
#endif
#endif

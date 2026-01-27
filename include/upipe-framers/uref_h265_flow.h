/*
 * Copyright (C) 2016-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe h265 flow definition attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_H265_FLOW_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_H265_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_VOID(h265_flow, tier, "h265.tier", tier)
UREF_ATTR_SMALL_UNSIGNED(h265_flow, profile, "h265.profile", profile)
UREF_ATTR_SMALL_UNSIGNED(h265_flow, profile_space, "h265.profilespace",
        profile space)
UREF_ATTR_UNSIGNED(h265_flow, profile_compatibility, "h265.profilecomp",
        profile compatibility)
UREF_ATTR_UNSIGNED(h265_flow, profile_constraint, "h265.profileconstraint",
        profile constraint)
UREF_ATTR_SMALL_UNSIGNED(h265_flow, level, "h265.level", level)

#ifdef __cplusplus
}
#endif
#endif

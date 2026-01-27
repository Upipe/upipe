/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe mpgv flow definition attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_MPGV_FLOW_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_MPGV_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(mpgv_flow, profilelevel, "mpgv.profilelevel",
        profile and level)

#ifdef __cplusplus
}
#endif
#endif

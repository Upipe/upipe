/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe mpgv attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_MPGV_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_MPGV_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(mpgv, type, "mpgv.type", picture coding type)

#ifdef __cplusplus
}
#endif
#endif

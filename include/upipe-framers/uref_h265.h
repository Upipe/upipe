/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe h265 attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_H265_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_H265_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(h265, type, "h265.type", slice coding type)

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe dvbsub attributes for uref
 */

#ifndef _UPIPE_FRAMERS_UREF_DVBSUB_FLOW_H_
/** @hidden */
#define _UPIPE_FRAMERS_UREF_DVBSUB_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(dvbsub_flow, pages, "dvbsub.pages", number of page);
UREF_ATTR_UNSIGNED_VA(dvbsub_flow, page, "dvbsub.page[%" PRIu8 "]", page id,
                      uint8_t nb, nb)

#ifdef __cplusplus
}
#endif
#endif

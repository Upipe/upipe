/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe flow definition attributes for TS SCTE 104
 */

#ifndef _UPIPE_TS_UREF_TS_SCTE104_FLOW_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_SCTE104_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

#include <string.h>
#include <stdint.h>

UREF_ATTR_UNSIGNED(ts_scte104_flow, dpi_pid_index, "scte104.dpi", DPI PID index)

#ifdef __cplusplus
}
#endif
#endif

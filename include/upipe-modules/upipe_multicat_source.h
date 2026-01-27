/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module - multicat file source
 */

#ifndef _UPIPE_MODULES_UPIPE_MULTICAT_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_MULTICAT_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "upipe/ubase.h"
#include "upipe/upipe.h"
#include "upipe/uref_attr.h"

UREF_ATTR_STRING(msrc_flow, path, "msrc.path", directory path)
UREF_ATTR_STRING(msrc_flow, data, "msrc.data", data suffix)
UREF_ATTR_STRING(msrc_flow, aux, "msrc.aux", aux suffix)
UREF_ATTR_UNSIGNED(msrc_flow, rotate, "msrc.rotate", rotate interval)
UREF_ATTR_UNSIGNED(msrc_flow, offset, "msrc.offset", rotate offset)

#define UPIPE_MSRC_SIGNATURE UBASE_FOURCC('m','s','r','c')
#define UPIPE_MSRC_DEF_ROTATE UINT64_C(97200000000)
#define UPIPE_MSRC_DEF_OFFSET UINT64_C(0)

/** @This returns the management structure for msrc pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_msrc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

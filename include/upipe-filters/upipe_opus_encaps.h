/*
 * Copyright (C) 2023 EasyTools S.A.S.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module encapsulating Opus access units
 */

#ifndef _UPIPE_FILTERS_UPIPE_OPUS_ENCAPS_H_
/** @hidden */
#define _UPIPE_FILTERS_UPIPE_OPUS_ENCAPS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_OPUS_ENCAPS_SIGNATURE UBASE_FOURCC('O','p','u','e')

/** @This returns the management structure for all ts_opus_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_opus_encaps_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif

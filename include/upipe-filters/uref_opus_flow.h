/*
 * Copyright (C) 2023 EasyTools S.A.S.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe opus flow definition attributes for uref
 */

#ifndef _UPIPE_FILTERS_UREF_OPUS_FLOW_H_
/** @hidden */
#define _UPIPE_FILTERS_UREF_OPUS_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/uref_flow.h"

/** @This defines encapsulation types for Opus. */
enum uref_opus_encaps {
    /** No encapsulation */
    UREF_OPUS_ENCAPS_RAW = 0,
    /** TS encapsulation */
    UREF_OPUS_ENCAPS_TS,
};

UREF_ATTR_SMALL_UNSIGNED(opus_flow, encaps, "opus.encaps",
                         Opus encapsulation type)

#ifdef __cplusplus
}
#endif
#endif

/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Uref attributes to use with upipe avfilter module
 */

#ifndef _UPIPE_AV_UREF_AVFILTER_FLOW_H_
#define _UPIPE_AV_UREF_AVFILTER_FLOW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"
#include "upipe/uref_flow.h"

/** @This define the void flow def prefix. */
#define UREF_AVFILT_FLOW_DEF  "avfilt."

UREF_ATTR_STRING(avfilt_flow, name, "avfilt.name", avfilter name)

/** @This allocates a new avfilt flow.
 *
 * @param mgr uref management structure
 * @return pointer to uref void flow
 */
static inline struct uref *uref_avfilt_flow_alloc_def(struct uref_mgr *mgr,
                                                      const char *name)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(!uref))
        return NULL;

    if (unlikely(!ubase_check(uref_flow_set_def(uref, UREF_AVFILT_FLOW_DEF)) ||
                 !ubase_check(uref_avfilt_flow_set_name(uref, name)))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#ifdef __cplusplus
}
#endif

#endif

/*
 * Copyright (c) 2017 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UREF_VOID_FLOW_H_
#define _UPIPE_UREF_VOID_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_flow.h"

/** @This define the void flow def prefix. */
#define UREF_VOID_FLOW_DEF  "void."

/** @This allocates a new void flow.
 *
 * @param mgr uref management structure
 * @return pointer to uref void flow
 */
static inline struct uref *uref_void_flow_alloc_def(struct uref_mgr *mgr)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(!uref))
        return NULL;

    if (unlikely(!ubase_check(uref_flow_set_def(uref, UREF_VOID_FLOW_DEF)))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#ifdef __cplusplus
}
#endif /* !_UPIPE_UREF_VOID_FLOW_H_ */
#endif

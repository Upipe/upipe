/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe program flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_PROGRAM_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_PROGRAM_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/uref_flow.h"

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

/** @internal flow definition prefix for programs */
#define UREF_PROGRAM_FLOW_DEF "void."

UREF_ATTR_STRING(program_flow, name, "prog.name", name of the program)

/** @This allocates a control packet to define a new program flow.
 *
 * @param mgr uref management structure
 * @return pointer to uref control packet, or NULL in case of error
 */
static inline struct uref *uref_program_flow_alloc_def(struct uref_mgr *mgr)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL))
        return NULL;

    if (unlikely(!ubase_check(uref_flow_set_def(uref,
                                                UREF_PROGRAM_FLOW_DEF)))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#undef UREF_PROGRAM_FLOW_DEF

#ifdef __cplusplus
}
#endif
#endif

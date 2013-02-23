/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe program flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_PROGRAM_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_PROGRAM_FLOW_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

/** @internal flow definition prefix for programs */
#define UREF_PROGRAM_FLOW_DEF "program."

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

    if (unlikely(!uref_flow_set_def(uref, UREF_PROGRAM_FLOW_DEF))) {
        uref_free(uref);
        return NULL;
    }
    return uref;
}

#undef UREF_PROGRAM_FLOW_DEF

#endif

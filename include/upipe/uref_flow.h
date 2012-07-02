/*****************************************************************************
 * uref_flow.h: flow attributes for uref and control messages
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_UREF_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_FLOW_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>
#include <stdbool.h>

UREF_ATTR_TEMPLATE(flow, name, "f.flow", string, const char *, flow name)
UREF_ATTR_TEMPLATE(flow, def, "f.def", string, const char *, flow definition)
UREF_ATTR_TEMPLATE_VOID(flow, delete, "f.delete", flow delete flag)
UREF_ATTR_TEMPLATE_VOID(flow, discontinuity, "f.disc", flow discontinuity flag)
UREF_ATTR_TEMPLATE(flow, lang, "f.lang", string, const char *, flow language)

/** @This sets the flow name attribute of a uref, with printf-style name
 * generation.
 *
 * @param uref_p reference to the pointer to the uref (possibly modified)
 * @param format printf-style format of the flow name, followed by a variable
 * list of arguments
 * @return true if no allocation failure occurred
 */
static inline bool uref_flow_set_name_va(struct uref **uref_p,
                                         const char *format, ...)
{
    UBASE_VARARG(uref_flow_set_name(uref_p, string))
}

/** @This duplicates a uref and sets the flow name attribute.
 *
 * @param mgr uref management structure
 * @param uref uref structure
 * @param flow flow name
 * @return pointer to new uref
 */
static inline struct uref *uref_flow_dup(struct uref_mgr *mgr,
                                         struct uref *uref, const char *flow)
{
    struct uref *new_uref = uref_dup(mgr, uref);
    if (unlikely(new_uref == NULL)) return NULL;
    if (unlikely(!(uref_flow_set_name(&new_uref, flow)))) {
        uref_release(new_uref);
        return NULL;
    }
    return new_uref;
}

/** @This allocates a control packet to delete a flow.
 *
 * @param mgr uref management structure
 * @param flow flow name
 * @return pointer to uref control packet
 */
static inline struct uref *uref_flow_alloc_delete(struct uref_mgr *mgr,
                                                  const char *flow)
{
    struct uref *uref = uref_alloc_control(mgr);
    if (unlikely(uref == NULL)) return NULL;
    if (unlikely(!(uref_flow_set_name(&uref, flow) &&
                   uref_flow_set_delete(&uref)))) {
        uref_release(uref);
        return NULL;
    }
    return uref;
}

#endif

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
 * @short Upipe flow attributes for uref and control messages
 */

#ifndef _UPIPE_UREF_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_FLOW_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <stdint.h>
#include <stdbool.h>

UREF_ATTR_TEMPLATE(flow, def, "f.def", string, const char *, flow definition)
UREF_ATTR_TEMPLATE(flow, program, "f.program", string, const char *,
                   flow program)
UREF_ATTR_TEMPLATE_VOID(flow, discontinuity, "f.disc", flow discontinuity flag)
UREF_ATTR_TEMPLATE(flow, lang, "f.lang", string, const char *, flow language)

/** @This sets the flow definition attribute of a uref, with printf-style
 * generation.
 *
 * @param uref uref structure
 * @param format printf-style format of the flow definition, followed by a
 * variable list of arguments
 * @return true if no allocation failure occurred
 */
static inline bool uref_flow_set_def_va(struct uref *uref,
                                        const char *format, ...)
{
    UBASE_VARARG(uref_flow_set_def(uref, string))
}

/** @This sets the flow program attribute of a uref, with printf-style program
 * generation.
 *
 * @param uref uref structure
 * @param format printf-style format of the flow program, followed by a variable
 * list of arguments
 * @return true if no allocation failure occurred
 */
static inline bool uref_flow_set_program_va(struct uref *uref,
                                            const char *format, ...)
{
    UBASE_VARARG(uref_flow_set_program(uref, string))
}

#endif

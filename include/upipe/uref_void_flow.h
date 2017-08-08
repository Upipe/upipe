/*
 * Copyright (c) 2017 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#ifndef _UPIPE_UREF_VOID_FLOW_H_
#define _UPIPE_UREF_VOID_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_flow.h>

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


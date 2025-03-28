/*
 * Copyright (C) 2019 OpenHeadend S.A.R.L.
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

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
 * @short Upipe flow definition attributes for TS
 */

#ifndef _UPIPE_UREF_TS_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_TS_FLOW_H_

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_TEMPLATE(ts_flow, pid, "t.pid", unsigned, uint64_t, PID)

/** @This returns the value of a PSI section filter.
 *
 * @param uref pointer to the uref
 * @param filter_p pointer to the retrieved filter (modified during execution)
 * @param mask_p pointer to the retrieved mask (modified during execution)
 * @param size_p size of the filter, written on execution
 * @return true if the attribute was found and is valid
 */
static inline bool uref_ts_flow_get_psi_filter(struct uref *uref,
                                               const uint8_t **filter_p,
                                               const uint8_t **mask_p,
                                               size_t *size_p)
{
    const uint8_t *attr;
    size_t size;
    if (unlikely(!uref_attr_get_opaque(uref, &attr, &size, "t.psi.filter") ||
                 !size || size % 2))
        return false;
    *size_p = size / 2;
    *filter_p = attr;
    *mask_p = attr + *size_p;
    return true;
}

/** @This sets the value of a PSI section filter, optionally creating it.
 *
 * @param uref pointer to the uref
 * @param filter section filter
 * @param mask section mask
 * @param size size (in octets) of filter and mask
 * @return true if no allocation failure occurred
 */
static inline bool uref_ts_flow_set_psi_filter(struct uref *uref,
                                               const uint8_t *filter,
                                               const uint8_t *mask,
                                               size_t size)
{
    uint8_t attr[2 * size];
    memcpy(attr, filter, size);
    memcpy(attr + size, mask, size);
    return uref_attr_set_opaque(uref, attr, 2 * size, "t.psi.filter");
}

/** @This deletes a PSI section filter.
 *
 * @param uref pointer to the uref
 * @return true if the attribute existed before
 */
static inline bool uref_ts_flow_delete_psi_filter(struct uref *uref)
{
    return uref_attr_delete_opaque(uref, "t.psi.filter");
}

#endif

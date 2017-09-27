/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 * @short Upipe h26x flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_H26X_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_H26X_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_flow.h>

/** @This defines encapsulation types for H26x */
enum uref_h26x_encaps {
    /** NAL units delimited by @ref uref_h26x_get_nal_offset */
    UREF_H26X_ENCAPS_NALU = 0,
    /** startcode-based, such as ISO 14496-10 annex B */
    UREF_H26X_ENCAPS_ANNEXB,
    /** unknown length, such as ISO 14496-15 */
    UREF_H26X_ENCAPS_LENGTH_UNKNOWN,
    /** 1 octet length, such as ISO 14496-15 */
    UREF_H26X_ENCAPS_LENGTH1,
    /** 2 octet length, such as ISO 14496-15 */
    UREF_H26X_ENCAPS_LENGTH2,
    /** 4 octet length, such as ISO 14496-15 */
    UREF_H26X_ENCAPS_LENGTH4
};

UREF_ATTR_SMALL_UNSIGNED(h26x_flow, encaps, "h26x.encaps", H26x encapsulation type)

/** @This infers the encapsulation type from a flow definition packet.
 *
 * @param flow_def flow definition packet
 * @return encapsulation type
 */
static inline enum uref_h26x_encaps
    uref_h26x_flow_infer_encaps(struct uref *flow_def)
{
    uint8_t encaps;
    const uint8_t *headers;
    size_t headers_size;
    if (ubase_check(uref_h26x_flow_get_encaps(flow_def, &encaps)))
        return encaps;
    else if (ubase_check(uref_flow_get_headers(flow_def, &headers,
                                               &headers_size)) &&
             (headers_size < 5 || headers[0] || headers[1]))
        return UREF_H26X_ENCAPS_LENGTH_UNKNOWN;
    else
        return UREF_H26X_ENCAPS_ANNEXB;
}

#ifdef __cplusplus
}
#endif
#endif

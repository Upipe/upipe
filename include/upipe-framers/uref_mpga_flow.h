/*
 * Copyright (C) 2015-2017 OpenHeadend S.A.R.L.
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
 * @short Upipe mpga flow definition attributes for uref
 */

#ifndef _UPIPE_UREF_MPGA_FLOW_H_
/** @hidden */
#define _UPIPE_UREF_MPGA_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

/** @This defines encapsulation types for AAC. */
enum uref_mpga_encaps {
    /** No encapsulation */
    UREF_MPGA_ENCAPS_RAW = 0,
    /** ADTS encapsulation */
    UREF_MPGA_ENCAPS_ADTS,
    /* LATM/LOAS */
    UREF_MPGA_ENCAPS_LOAS,
    /* LATM (aligned AudioMuxElement with mux config) */
    UREF_MPGA_ENCAPS_LATM
};

UREF_ATTR_SMALL_UNSIGNED(mpga_flow, encaps, "mpga.encaps", AAC encapsulation type)
UREF_ATTR_SMALL_UNSIGNED(mpga_flow, mode, "mpga.mode", MPEG audio mode)

/** @This infers the encapsulation type from a flow definition packet.
 *
 * @param flow_def flow definition packet
 * @return encapsulation type
 */
static inline enum uref_mpga_encaps
    uref_mpga_flow_infer_encaps(struct uref *flow_def)
{
    uint8_t encaps;
    if (ubase_check(uref_mpga_flow_get_encaps(flow_def, &encaps)))
        return encaps;
    else if (ubase_check(uref_flow_get_global(flow_def)))
        return UREF_MPGA_ENCAPS_RAW;
    return UREF_MPGA_ENCAPS_ADTS;
}

/** @This encodes an encapsulation from a string.
 *
 * @param encaps string describing the encapsulation
 * @return codec encapsulation
 */
static inline enum uref_mpga_encaps
    uref_mpga_encaps_from_string(const char *encaps)
{
    if (encaps == NULL)
        return UREF_MPGA_ENCAPS_ADTS;
    if (!strcmp(encaps, "latm"))
        return UREF_MPGA_ENCAPS_LATM;
    if (!strcmp(encaps, "loas"))
        return UREF_MPGA_ENCAPS_LOAS;
    if (!strcmp(encaps, "raw"))
        return UREF_MPGA_ENCAPS_RAW;
    return UREF_MPGA_ENCAPS_ADTS;
}

#ifdef __cplusplus
}
#endif
#endif

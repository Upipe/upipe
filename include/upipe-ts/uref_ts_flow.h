/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_UNSIGNED(ts_flow, pid, "t.pid", PID)
UREF_ATTR_UNSIGNED(ts_flow, pcr_pid, "t.pcr_pid", PCR PID)
UREF_ATTR_UNSIGNED(ts_flow, ts_delay, "t.ts_delay", T-STD TS delay (TB buffer))
UREF_ATTR_UNSIGNED(ts_flow, max_delay, "t.maxdelay", maximum retention time)
UREF_ATTR_UNSIGNED(ts_flow, tb_rate, "t.tbrate", T-STD TB emptying rate)
UREF_ATTR_OPAQUE(ts_flow, psi_filter_internal, "t.psi.filter", PSI filter)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, pes_id, "t.pes_id", PES stream ID)
UREF_ATTR_VOID(ts_flow, pes_alignment, "t.pes_align", PES data alignment)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, pes_header, "t.pes_header",
        minimum PES header size)
UREF_ATTR_UNSIGNED(ts_flow, pes_min_duration, "t.pes_mindur",
        minimum PES duration)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, descriptors, "t.descs", number of descriptors)
UREF_ATTR_OPAQUE_VA(ts_flow, descriptor, "t.desc[%"PRIu8"]", descriptor,
        uint8_t nb, nb)
UREF_ATTR_UNSIGNED(ts_flow, stream_type, "t.streamtype", stream type)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_flow, telx_type, "t.telxtype[%"PRIu8"]",
        teletext type according to EN 300 468, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_flow, telx_magazine, "t.telxmag[%"PRIu8"]",
        teletext magazine according to EN 300 468, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_flow, telx_page, "t.telxpage[%"PRIu8"]",
        teletext page according to EN 300 468, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_flow, sub_type, "t.subtype[%"PRIu8"]",
        subtitling type according to EN 300 468, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_flow, sub_composition, "t.subcomp[%"PRIu8"]",
        subtitling composition page according to EN 300 468, uint8_t nb, nb)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_flow, sub_ancillary, "t.subanc[%"PRIu8"]",
        subtitling ancillary page according to EN 300 468, uint8_t nb, nb)

/** @This returns the value of a PSI section filter.
 *
 * @param uref pointer to the uref
 * @param filter_p pointer to the retrieved filter (modified during execution)
 * @param mask_p pointer to the retrieved mask (modified during execution)
 * @param size_p size of the filter, written on execution
 * @return an error code
 */
static inline int uref_ts_flow_get_psi_filter(struct uref *uref,
        const uint8_t **filter_p, const uint8_t **mask_p, size_t *size_p)
{
    const uint8_t *attr;
    size_t size;
    UBASE_RETURN(uref_ts_flow_get_psi_filter_internal(uref, &attr, &size))
    if (unlikely(!size || size % 2))
        return UBASE_ERR_INVALID;
    *size_p = size / 2;
    *filter_p = attr;
    *mask_p = attr + *size_p;
    return UBASE_ERR_NONE;
}

/** @This sets the value of a PSI section filter, optionally creating it.
 *
 * @param uref pointer to the uref
 * @param filter section filter
 * @param mask section mask
 * @param size size (in octets) of filter and mask
 * @return an error code
 */
static inline int uref_ts_flow_set_psi_filter(struct uref *uref,
        const uint8_t *filter, const uint8_t *mask, size_t size)
{
    uint8_t attr[2 * size];
    memcpy(attr, filter, size);
    memcpy(attr + size, mask, size);
    return uref_ts_flow_set_psi_filter_internal(uref, attr, 2 * size);
}

/** @This deletes a PSI section filter.
 *
 * @param uref pointer to the uref
 * @return an error code
 */
static inline int uref_ts_flow_delete_psi_filter(struct uref *uref)
{
    return uref_ts_flow_delete_psi_filter_internal(uref);
}

/** @This registers a new descriptor in the TS flow definition packet.
 *
 * @param uref pointer to the uref
 * @param desc descriptor
 * @param desc_len size of descriptor
 * @return an error code
 */
static inline enum ubase_err uref_ts_flow_add_descriptor(struct uref *uref,
        const uint8_t *desc, size_t desc_len)
{
    uint8_t descriptors = 0;
    uref_ts_flow_get_descriptors(uref, &descriptors);
    UBASE_RETURN(uref_ts_flow_set_descriptors(uref, descriptors + 1))
    UBASE_RETURN(uref_ts_flow_set_descriptor(uref, desc, desc_len, descriptors))
    return UBASE_ERR_NONE;
}

/** @This gets the total size of descriptors.
 *
 * @param uref pointer to the uref
 * @return the size of descriptors
 */
static inline size_t uref_ts_flow_size_descriptors(struct uref *uref)
{
    uint8_t descriptors = 0;
    uref_ts_flow_get_descriptors(uref, &descriptors);
    size_t descs_len = 0;
    for (uint8_t j = 0; j < descriptors; j++) {
        const uint8_t *desc;
        size_t desc_len;
        if (ubase_check(uref_ts_flow_get_descriptor(uref, &desc, &desc_len, j)))
            descs_len += desc_len;
    }
    return descs_len;
}

/** @This extracts all descriptors.
 *
 * @param uref pointer to the uref
 * @param descs_p filled in with the descriptors (size to be calculated with
 * @ref uref_ts_flow_size_descriptors)
 */
static inline void uref_ts_flow_extract_descriptors(struct uref *uref,
                                                    uint8_t *descs_p)
{
    uint8_t descriptors = 0;
    uref_ts_flow_get_descriptors(uref, &descriptors);
    for (uint8_t j = 0; j < descriptors; j++) {
        const uint8_t *desc;
        size_t desc_len;
        if (ubase_check(uref_ts_flow_get_descriptor(uref, &desc, &desc_len,
                                                    j))) {
            memcpy(descs_p, desc, desc_len);
            descs_p += desc_len;
        }
    }
}

#ifdef __cplusplus
}
#endif
#endif

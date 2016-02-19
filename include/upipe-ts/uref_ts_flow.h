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

#ifndef _UPIPE_TS_UREF_TS_FLOW_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_FLOW_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe-ts/uref_ts_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_STRING(ts_flow, conformance, "t.conf", conformance)
UREF_ATTR_UNSIGNED(ts_flow, pid, "t.pid", PID)
UREF_ATTR_UNSIGNED(ts_flow, pcr_pid, "t.pcr_pid", PCR PID)
UREF_ATTR_UNSIGNED(ts_flow, max_delay, "t.maxdelay", maximum retention time)
UREF_ATTR_UNSIGNED(ts_flow, tb_rate, "t.tbrate", T-STD TB emptying rate)
UREF_ATTR_OPAQUE(ts_flow, psi_filter_internal, "t.psi.filter", PSI filter)
UREF_ATTR_UNSIGNED(ts_flow, psi_section_interval, "t.psi.sec",
        interval between PSI sections)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, pes_id, "t.pes_id", PES stream ID)
UREF_ATTR_VOID(ts_flow, pes_alignment, "t.pes_align", PES data alignment)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, pes_header, "t.pes_header",
        minimum PES header size)
UREF_ATTR_UNSIGNED(ts_flow, pes_min_duration, "t.pes_mindur",
        minimum PES duration)

/* PMT */
UREF_ATTR_SMALL_UNSIGNED(ts_flow, component_type, "t.ctype", component type)
UREF_ATTR_UNSIGNED(ts_flow, descriptors, "t.descs", number of descriptors)
UREF_ATTR_OPAQUE_VA(ts_flow, descriptor, "t.desc[%"PRIu64"]", descriptor,
        uint64_t nb, nb)
UREF_TS_ATTR_DESCRIPTOR(ts_flow, descriptor)
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

/* SDT */
UREF_ATTR_UNSIGNED(ts_flow, tsid, "t.tsid", transport stream ID)
UREF_ATTR_UNSIGNED(ts_flow, onid, "t.onid", original network ID)
UREF_ATTR_VOID(ts_flow, eit, "t.eit", presence of EITp/f)
UREF_ATTR_VOID(ts_flow, eit_schedule, "t.eits", presence of EIT schedule)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, running_status, "t.run", running status)
UREF_ATTR_VOID(ts_flow, scrambled, "t.ca", scrambled service)
UREF_ATTR_STRING(ts_flow, provider_name, "t.provname", provider name)
UREF_ATTR_SMALL_UNSIGNED(ts_flow, service_type, "t.servtype", service type)
UREF_ATTR_UNSIGNED(ts_flow, sdt_descriptors, "t.sdt.descs",
        number of SDT descriptors)
UREF_ATTR_OPAQUE_VA(ts_flow, sdt_descriptor, "t.sdt.desc[%"PRIu64"]",
        SDT descriptor, uint64_t nb, nb)
UREF_TS_ATTR_DESCRIPTOR(ts_flow, sdt_descriptor)

/* EIT */
UREF_ATTR_SMALL_UNSIGNED(ts_flow, last_table_id, "t.lasttid",
        last table ID)

/* NIT */
UREF_ATTR_UNSIGNED(ts_flow, nid, "t.nid", network ID)
UREF_ATTR_STRING(ts_flow, network_name, "t.netwname", network name)
UREF_ATTR_UNSIGNED(ts_flow, nit_descriptors, "t.nit.descs",
        number of NIT descriptors)
UREF_ATTR_OPAQUE_VA(ts_flow, nit_descriptor, "t.nit.desc[%"PRIu64"]",
        NIT descriptor, uint64_t nb, nb)
UREF_TS_ATTR_DESCRIPTOR(ts_flow, nit_descriptor)
UREF_ATTR_UNSIGNED(ts_flow, nit_ts, "t.nit.ts", ts number)
UREF_ATTR_UNSIGNED_VA(ts_flow, nit_ts_tsid, "t.nit.tstsid[%"PRIu64"]",
        ts transport stream ID, uint64_t ts, ts)
UREF_ATTR_UNSIGNED_VA(ts_flow, nit_ts_onid, "t.nit.tsonid[%"PRIu64"]",
        ts original network ID, uint64_t ts, ts)
UREF_ATTR_UNSIGNED_VA(ts_flow, nit_ts_descriptors,
        "t.nit.tsdescs[%"PRIu64"]", number of NIT TS descriptors,
        uint64_t ts, ts)
UREF_TS_ATTR_SUBDESCRIPTOR(ts_flow, nit_ts_descriptor,
        "t.nit.tsdesc[%"PRIu64"][%"PRIu64"]")

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

#ifdef __cplusplus
}
#endif
#endif

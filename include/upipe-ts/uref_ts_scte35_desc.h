/*
 * Copyright (C) 2020-2025 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe uref attributes for TS SCTE 35 descriptors
 */

#ifndef _UPIPE_TS_UREF_TS_SCTE35_DESC_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_SCTE35_DESC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"

#include <string.h>
#include <stdint.h>

/* splice descriptor */
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc, tag, "scte35.desc.tag", tag)
UREF_ATTR_UNSIGNED(ts_scte35_desc, identifier, "scte35.desc.id", identifier)

/* avail splice descriptor */
UREF_ATTR_UNSIGNED(ts_scte35_desc_avail, provider_avail_id,
                   "scte35.desc.avail.provider_avail_id", provider avail id)

/* segmentation splice descriptor*/
UREF_ATTR_UNSIGNED(ts_scte35_desc_seg, event_id, "scte35.desc.seg.event_id",
                   segmentation event id);
UREF_ATTR_VOID(ts_scte35_desc_seg, cancel, "scte35.desc.seg.cancel",
               segmentation event cancel indicator);
UREF_ATTR_VOID(ts_scte35_desc_seg, delivery_not_restricted,
               "scte35.desc.seg.delivery_not_restricted",
               delivery not restricted);
UREF_ATTR_VOID(ts_scte35_desc_seg, web, "scte35.desc.seg.web",
               web delivery allowed);
UREF_ATTR_VOID(ts_scte35_desc_seg, no_regional_blackout,
               "scte35.desc.seg.no_regional_blackout",
               no regional blackout);
UREF_ATTR_VOID(ts_scte35_desc_seg, archive, "scte35.desc.seg.archive",
               archive allowed);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, device, "scte35.desc.seg.device",
                         device restrictions);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, nb_comp, "scte35.desc.seg.nb_comp",
                         component count);
UREF_ATTR_SMALL_UNSIGNED_VA(ts_scte35_desc_seg_comp, tag,
                            "scte35.desc.seg.comp[%u].tag",
                            component tag, uint8_t comp, comp);
UREF_ATTR_UNSIGNED_VA(ts_scte35_desc_seg_comp, pts_off,
                      "scte35.desc.seg.comp[%u].pts_off",
                      component PTS offset, uint8_t comp, comp);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, upid_type,
                         "scte35.desc.seg.upid_type",
                         segmentation upid type);
UREF_ATTR_STRING(ts_scte35_desc_seg, upid_type_name,
                 "scte35.desc.seg.upid_type_name",
                 segmentation upid type name);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, upid_length,
                         "scte35.desc.seg.upid_length",
                         segmentation upid length);
UREF_ATTR_OPAQUE(ts_scte35_desc_seg, upid, "scte35.desc.seg.upid",
                 segmentation upid);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, type_id, "scte35.desc.seg.type_id",
                         segmentation type id);
UREF_ATTR_STRING(ts_scte35_desc_seg, type_id_name,
                 "scte35.desc.seg.type_id_name",
                 segmentation type id name);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, num, "scte35.desc.seg.num",
                         segment num);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, expected,
                         "scte35.desc.seg.expected",
                         segments expected);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, sub_num, "scte35.desc.seg.sub_num",
                         sub segment num);
UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc_seg, sub_expected,
                         "scte35.desc.seg.sub_expected",
                         sub segment expected);

/* time splice descriptor */
UREF_ATTR_UNSIGNED(ts_scte35_desc_time, tai_sec, "scte35.desc.time.tai_sec",
                   seconds part of the TAI);
UREF_ATTR_UNSIGNED(ts_scte35_desc_time, tai_nsec, "scte35.desc.time.tai_nsec",
                   nanoseconds part of the TAI);
UREF_ATTR_UNSIGNED(ts_scte35_desc_time, utc_off, "scte35.desc.time.utc_off",
                   offset from UTC in seconds);

#ifdef __cplusplus
}
#endif
#endif

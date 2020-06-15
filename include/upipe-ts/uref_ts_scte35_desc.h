/*
 * Copyright (C) 2020 EasyTools
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
 * @short Upipe uref attributes for TS SCTE 35 descriptors
 */

#ifndef _UPIPE_TS_UREF_TS_SCTE35_DESC_H_
/** @hidden */
#define _UPIPE_TS_UREF_TS_SCTE35_DESC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_SMALL_UNSIGNED(ts_scte35_desc, tag, "scte35.desc.tag", tag)
UREF_ATTR_UNSIGNED(ts_scte35_desc, identifier, "scte35.desc.id", identifier)
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

#ifdef __cplusplus
}
#endif
#endif

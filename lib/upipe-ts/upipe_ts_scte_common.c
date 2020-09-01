/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * This event is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This event is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this event; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Common function for SCTE decoders
 * Normative references:
 *  - SCTE 104 2012 (Automation to Compression Communications API)
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uref_clock.h>
#include <upipe-ts/uref_ts_scte35_desc.h>

#include <bitstream/scte/35.h>

#include "upipe_ts_scte_common.h"

/** ratio between Upipe freq and MPEG freq */
#define CLOCK_SCALE (UCLOCK_FREQ / 90000)

/** @This allocates an uref describing a SCTE35 descriptor.
 *
 * @param upipe description structure of the caller
 * @param uref input buffer
 * @param desc pointer to the SCTE35 descriptor
 * @return an allocated uref of NULL
 */
struct uref *upipe_ts_scte_extract_desc(struct upipe *upipe,
                                        struct uref *uref,
                                        const uint8_t *desc)
{
    struct uref *out = uref_dup_inner(uref);
    if (!out) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }
    uint8_t tag = scte35_splice_desc_get_tag(desc);
    uint8_t length = scte35_splice_desc_get_length(desc);

    if (length < SCTE35_SPLICE_DESC_HEADER_SIZE) {
        uref_free(out);
        return NULL;
    }
    uint32_t identifier = scte35_splice_desc_get_identifier(desc);

    uref_ts_scte35_desc_set_tag(out, tag);
    uref_ts_scte35_desc_set_identifier(out, identifier);

    switch (tag) {
        case SCTE35_SPLICE_DESC_TAG_SEG: {
            uint32_t seg_event_id = scte35_seg_desc_get_event_id(desc);
            bool cancel = scte35_seg_desc_has_cancel(desc);
            uref_ts_scte35_desc_seg_set_event_id(out, seg_event_id);
            if (cancel)
                uref_ts_scte35_desc_seg_set_cancel(out);
            else {
                bool has_delivery_not_restricted =
                    scte35_seg_desc_has_delivery_not_restricted(desc);
                if (!has_delivery_not_restricted) {
                    bool has_web_delivery_allowed =
                        scte35_seg_desc_has_web_delivery_allowed(desc);
                    bool has_no_regional_blackout =
                        scte35_seg_desc_has_no_regional_blackout(desc);
                    bool has_archive_allowed =
                        scte35_seg_desc_has_archive_allowed(desc);
                    uint8_t device_restrictions =
                        scte35_seg_desc_get_device_restrictions(desc);
                    if (has_web_delivery_allowed)
                        uref_ts_scte35_desc_seg_set_web(out);
                    if (has_no_regional_blackout)
                        uref_ts_scte35_desc_seg_set_no_regional_blackout(
                            out);
                    if (has_archive_allowed)
                        uref_ts_scte35_desc_seg_set_archive(out);
                    uref_ts_scte35_desc_seg_set_device(
                        out, device_restrictions);
                }
                else
                    uref_ts_scte35_desc_seg_set_delivery_not_restricted(
                        out);

                bool has_program_seg =
                    scte35_seg_desc_has_program_seg(desc);
                if (!has_program_seg) {
                    uint8_t nb_comp =
                        scte35_seg_desc_get_component_count(desc);
                    uref_ts_scte35_desc_seg_set_nb_comp(out, nb_comp);
                    for (uint8_t j = 0; j < nb_comp; j++) {
                        const uint8_t *comp =
                            scte35_seg_desc_get_component(desc, j);
                        uint8_t comp_tag =
                            scte35_seg_desc_component_get_tag(comp);
                        uint64_t pts_off =
                            scte35_seg_desc_component_get_pts_off(comp);
                        uref_ts_scte35_desc_seg_comp_set_tag(
                            out, comp_tag, j);
                        uref_ts_scte35_desc_seg_comp_set_pts_off(
                            out, pts_off, j);
                    }
                }

                bool has_duration = scte35_seg_desc_has_duration(desc);
                if (has_duration) {
                    uint64_t duration = scte35_seg_desc_get_duration(desc);
                    uref_clock_set_duration(out, duration * CLOCK_SCALE);
                }

                uint8_t upid_type = scte35_seg_desc_get_upid_type(desc);
                uint8_t upid_length = scte35_seg_desc_get_upid_length(desc);
                const uint8_t *upid = scte35_seg_desc_get_upid(desc);
                uint8_t type_id = scte35_seg_desc_get_type_id(desc);
                uint8_t num = scte35_seg_desc_get_num(desc);
                uint8_t expected = scte35_seg_desc_get_expected(desc);
                if (upid_type || upid_length) {
                    uref_ts_scte35_desc_seg_set_upid_type(out, upid_type);
                    uref_ts_scte35_desc_seg_set_upid_type_name(
                        out, scte35_seg_desc_upid_type_to_str(upid_type));
                    uref_ts_scte35_desc_seg_set_upid_length(
                        out, upid_length);
                    uref_ts_scte35_desc_seg_set_upid(
                        out, upid, upid_length);
                }
                uref_ts_scte35_desc_seg_set_type_id(out, type_id);
                uref_ts_scte35_desc_seg_set_type_id_name(
                    out, scte35_seg_desc_type_id_to_str(type_id));
                uref_ts_scte35_desc_seg_set_num(out, num);
                uref_ts_scte35_desc_seg_set_expected(out, expected);
                if (scte35_seg_desc_has_sub_num(desc)) {
                    uint8_t sub_num =
                        scte35_seg_desc_get_sub_num(desc);
                    uref_ts_scte35_desc_seg_set_sub_num(out, sub_num);
                }
                if (scte35_seg_desc_has_sub_expected(desc)) {
                    uint8_t sub_expected =
                        scte35_seg_desc_get_sub_expected(desc);
                    uref_ts_scte35_desc_seg_set_sub_expected(
                        out, sub_expected);
                }
            }
            break;
        }
    }
    return out;
}

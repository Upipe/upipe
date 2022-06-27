/*
 * Copyright (C) 2022 EasyTools
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

#include "upipe/uclock.h"
#include "upipe/uref_clock.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe-ts/uref_ts_scte35.h"
#include "upipe-ts/uref_ts_scte35_desc.h"

#include <bitstream/scte/35.h>

/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** ratio between Upipe freq and MPEG freq */
#define CLOCK_SCALE (UCLOCK_FREQ / 90000)

/** @This extracts a segmentation descriptor and checks its validity.
 *
 * @param uref uref describing time signal event
 * @param desc_p pointer filled with descriptor
 * @param desc_len_p pointer filled with descriptor length
 * @param at index of the descriptor to extract
 * @return an error code
 */
int uref_ts_scte35_desc_get_seg(struct uref *uref,
                                const uint8_t **desc_p,
                                size_t *desc_len_p,
                                uint64_t at)
{
    uint64_t nb = 0;
    UBASE_RETURN(uref_ts_flow_get_descriptors(uref, &nb));
    if (at >= nb)
        return UBASE_ERR_INVALID;

    const uint8_t *desc = NULL;
    size_t desc_len = 0;
    UBASE_RETURN(uref_ts_flow_get_descriptor(uref, &desc, &desc_len, at));
    if (!desc)
        return UBASE_ERR_INVALID;

    if (desc_len < SCTE35_SPLICE_DESC_HEADER_SIZE + SCTE35_SEG_DESC_HEADER_SIZE)
        return UBASE_ERR_INVALID;

    uint8_t length = scte35_splice_desc_get_length(desc) + DESC_HEADER_SIZE;
    if (length != desc_len)
        return UBASE_ERR_INVALID;

    uint32_t identifier = scte35_splice_desc_get_identifier(desc);
    if (identifier != SCTE35_SPLICE_DESC_IDENTIFIER)
        return UBASE_ERR_INVALID;

    if (desc_p)
        *desc_p = desc;
    if (desc_len_p)
        *desc_len_p = desc_len;
    return UBASE_ERR_NONE;
}

/** @This allocates an uref describing a SCTE35 descriptor.
 *
 * @param uref input buffer
 * @param at index of the descriptor to extract
 * @return an allocated uref of NULL
 */
struct uref *uref_ts_scte35_extract_desc(struct uref *uref, uint64_t at)
{
    const uint8_t *desc;
    size_t length;
    int ret = uref_ts_scte35_desc_get_seg(uref, &desc, &length, at);
    if (unlikely(!ubase_check(ret)))
        return NULL;

    uint8_t tag = scte35_splice_desc_get_tag(desc);

    struct uref *out = uref_sibling_alloc_control(uref);
    if (!out)
        return NULL;

    uref_ts_scte35_desc_set_tag(out, tag);
    uref_ts_scte35_desc_set_identifier(out, SCTE35_SPLICE_DESC_IDENTIFIER);

    length -= SCTE35_SPLICE_DESC_HEADER_SIZE;

    switch (tag) {
        case SCTE35_SPLICE_DESC_TAG_SEG: {
            if (length < SCTE35_SEG_DESC_HEADER_SIZE) {
                uref_free(out);
                return NULL;
            }
            length -= SCTE35_SEG_DESC_HEADER_SIZE;

            uint32_t seg_event_id = scte35_seg_desc_get_event_id(desc);
            bool cancel = scte35_seg_desc_has_cancel(desc);
            uref_ts_scte35_desc_seg_set_event_id(out, seg_event_id);
            if (cancel)
                uref_ts_scte35_desc_seg_set_cancel(out);
            else {
                if (length < SCTE35_SEG_DESC_NO_CANCEL_SIZE) {
                    uref_free(out);
                    return NULL;
                }
                length -= SCTE35_SEG_DESC_NO_CANCEL_SIZE;

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
                    if (length < SCTE35_SEG_DESC_NO_PROG_SEG_SIZE) {
                        uref_free(out);
                        return NULL;
                    }
                    length -= SCTE35_SEG_DESC_NO_PROG_SEG_SIZE;

                    uint8_t nb_comp =
                        scte35_seg_desc_get_component_count(desc);
                    if (length < nb_comp * SCTE35_SEG_DESC_COMPONENT_SIZE) {
                        uref_free(out);
                        return NULL;
                    }
                    length -= nb_comp * SCTE35_SEG_DESC_COMPONENT_SIZE;
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
                    if (length < SCTE35_SEG_DESC_DURATION_SIZE) {
                        uref_free(out);
                        return NULL;
                    }
                    length -= SCTE35_SEG_DESC_DURATION_SIZE;

                    uint64_t duration = scte35_seg_desc_get_duration(desc);
                    uref_clock_set_duration(out, duration * CLOCK_SCALE);
                }

                uint8_t upid_type = scte35_seg_desc_get_upid_type(desc);
                uint8_t upid_length = scte35_seg_desc_get_upid_length(desc);
                if (length < upid_length) {
                    uref_free(out);
                    return NULL;
                }
                length -= upid_length;
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
                if (length >= SCTE35_SEG_DESC_SUB_SEG_SIZE) {
                    length -= SCTE35_SEG_DESC_SUB_SEG_SIZE;
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
            }
            break;
        }
    }
    return out;
}

/** @This export an uref describing a SCTE35 descriptor.
 *
 * @param dst destination uref
 * @param uref uref to export as a descriptor
 * @return an error code
 */
int uref_ts_scte35_add_desc(struct uref *dst, struct uref *uref)
{
    if (!dst || !uref)
        return UBASE_ERR_INVALID;

    uint8_t desc[PSI_MAX_SIZE + PSI_HEADER_SIZE];
    uint32_t desc_len = PSI_MAX_SIZE;

    uint8_t tag;
    UBASE_RETURN(uref_ts_scte35_desc_get_tag(uref, &tag));
    uint64_t identifier;
    UBASE_RETURN(uref_ts_scte35_desc_get_identifier(uref, &identifier));

    switch (tag) {
        case SCTE35_SPLICE_DESC_TAG_SEG: {
            uint32_t length = 0;
            uint64_t event_id;
            UBASE_RETURN(uref_ts_scte35_desc_seg_get_event_id(uref, &event_id));
            bool cancel = ubase_check(uref_ts_scte35_desc_seg_get_cancel(uref));
            bool has_delivery_not_restricted =
                ubase_check(
                    uref_ts_scte35_desc_seg_get_delivery_not_restricted(uref));
            bool has_web_delivery_allowed =
                ubase_check(uref_ts_scte35_desc_seg_get_web(uref));
            bool has_no_regional_blackout =
                ubase_check(
                    uref_ts_scte35_desc_seg_get_no_regional_blackout(
                        uref));
            bool has_archive_allowed =
                ubase_check(uref_ts_scte35_desc_seg_get_archive(uref));
            uint8_t device_restrictions = 0;
            if (!cancel && !has_delivery_not_restricted)
                UBASE_RETURN(uref_ts_scte35_desc_seg_get_device(
                        uref, &device_restrictions));
            uint8_t nb_comp = 0;
            bool has_program_seg =
                !ubase_check(uref_ts_scte35_desc_seg_get_nb_comp(uref, &nb_comp));
            uint64_t duration = UINT64_MAX;
            bool has_duration =
                ubase_check(uref_clock_get_duration(uref, &duration));

            uint8_t upid_type = 0;
            uref_ts_scte35_desc_seg_get_upid_type(uref, &upid_type);
            const uint8_t *upid = NULL;
            size_t upid_length = 0;
            uref_ts_scte35_desc_seg_get_upid(uref, &upid, &upid_length);

            uint8_t type_id = 0;
            uint8_t num = 0;
            uint8_t expected = 0;
            if (!cancel) {
                UBASE_RETURN(
                    uref_ts_scte35_desc_seg_get_type_id(uref, &type_id));
                UBASE_RETURN(uref_ts_scte35_desc_seg_get_num(uref, &num));
                UBASE_RETURN(
                    uref_ts_scte35_desc_seg_get_expected(uref, &expected));
            }
            uint8_t sub_num = 0;
            bool has_sub_num =
                ubase_check(
                    uref_ts_scte35_desc_seg_get_sub_num(uref, &sub_num));
            uint8_t sub_expected = 0;
            bool has_sub_expected =
                ubase_check(uref_ts_scte35_desc_seg_get_sub_expected(
                        uref, &sub_expected));

            if (!cancel) {
                length += SCTE35_SEG_DESC_NO_CANCEL_SIZE;

                if (!has_program_seg) {
                    length += SCTE35_SEG_DESC_NO_PROG_SEG_SIZE +
                        SCTE35_SEG_DESC_COMPONENT_SIZE * nb_comp;
                }

                if (has_duration)
                    length += SCTE35_SEG_DESC_DURATION_SIZE;

                length += upid_length;

                if (has_sub_num && has_sub_expected)
                    length += SCTE35_SEG_DESC_SUB_SEG_SIZE;
            }

            if (length > desc_len)
                return UBASE_ERR_NOSPC;
            scte35_seg_desc_init(desc, length);
            scte35_seg_desc_set_event_id(desc, event_id);
            scte35_seg_desc_set_cancel(desc, cancel);
            scte35_seg_desc_set_program_seg(desc, has_program_seg);
            scte35_seg_desc_set_has_duration(desc, has_duration);
            scte35_seg_desc_set_delivery_not_restricted(
                desc, has_delivery_not_restricted);
            scte35_seg_desc_set_web_delivery_allowed(
                desc, has_web_delivery_allowed);
            scte35_seg_desc_set_no_regional_blackout(
                desc, has_no_regional_blackout);
            scte35_seg_desc_set_archive_allowed(
                desc, has_archive_allowed);
            scte35_seg_desc_set_device_restrictions(desc, device_restrictions);
            scte35_seg_desc_set_component_count(desc, nb_comp);
            for (uint8_t i = 0; i < nb_comp; i++) {
                uint8_t *comp = scte35_seg_desc_get_component(desc, i);
                if (!comp)
                    continue;
                uint8_t comp_tag;
                uint64_t pts_off;
                UBASE_RETURN(uref_ts_scte35_desc_seg_comp_get_tag(
                        uref, &comp_tag, i));
                UBASE_RETURN(uref_ts_scte35_desc_seg_comp_get_pts_off(
                        uref, &pts_off, i));
                scte35_seg_desc_component_init(comp);
                scte35_seg_desc_component_set_tag(comp, comp_tag);
                scte35_seg_desc_component_set_pts_off(comp, pts_off);
            }
            scte35_seg_desc_set_duration(desc,
                                         (duration / CLOCK_SCALE) % POW2_33);
            scte35_seg_desc_set_upid_type(desc, upid_type);
            scte35_seg_desc_set_upid_length(desc, upid_length);
            uint8_t *upid_ptr = scte35_seg_desc_get_upid(desc);
            if (upid_ptr)
                memcpy(upid_ptr, upid, upid_length);
            scte35_seg_desc_set_type_id(desc, type_id);
            scte35_seg_desc_set_num(desc, num);
            scte35_seg_desc_set_expected(desc, expected);
            scte35_seg_desc_set_sub_num(desc, sub_num);
            scte35_seg_desc_set_sub_expected(desc, sub_expected);
            uref_ts_flow_add_descriptor(
                dst, desc, desc_get_length(desc) + DESC_HEADER_SIZE);
            break;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
    return UBASE_ERR_NONE;
}

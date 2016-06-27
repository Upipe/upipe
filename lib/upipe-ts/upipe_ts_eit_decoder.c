/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module decoding the event description table of DVB streams
 * Normative references:
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_event.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_iconv.h>
#include <upipe-ts/upipe_ts_eit_decoder.h>
#include <upipe-ts/uref_ts_event.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <iconv.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtseit."
/** we only store UTF-8 */
#define NATIVE_ENCODING "UTF-8"

/** @hidden */
static int upipe_ts_eitd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_eitd pipe. */
struct upipe_ts_eitd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** input flow definition */
    struct uref *flow_def_input;
    /** attributes in the sequence header */
    struct uref *flow_def_attr;

    /** currently in effect EIT table */
    UPIPE_TS_PSID_TABLE_DECLARE(eit);
    /** EIT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_eit);

    /** encoding of the following iconv handle */
    const char *current_encoding;
    /** iconv handle */
    iconv_t iconv_handle;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_eitd, upipe, UPIPE_TS_EITD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_eitd, urefcount, upipe_ts_eitd_free)
UPIPE_HELPER_VOID(upipe_ts_eitd)
UPIPE_HELPER_OUTPUT(upipe_ts_eitd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_eitd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_eitd_check,
                      upipe_ts_eitd_register_output_request,
                      upipe_ts_eitd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_eitd, flow_def_input, flow_def_attr)
UPIPE_HELPER_ICONV(upipe_ts_eitd, NATIVE_ENCODING, current_encoding, iconv_handle)

/** @internal @This allocates a ts_eitd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_eitd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_eitd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_eitd *upipe_ts_eitd = upipe_ts_eitd_from_upipe(upipe);
    upipe_ts_eitd_init_urefcount(upipe);
    upipe_ts_eitd_init_output(upipe);
    upipe_ts_eitd_init_ubuf_mgr(upipe);
    upipe_ts_eitd_init_flow_def(upipe);
    upipe_ts_eitd_init_iconv(upipe);
    upipe_ts_psid_table_init(upipe_ts_eitd->eit);
    upipe_ts_psid_table_init(upipe_ts_eitd->next_eit);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This inserts a new section that composes a table. This is forked off
 * @ref upipe_ts_psid_table_section because the EIT may contain holes.
 *
 * @param sections PSI table
 * @param uref new section
 * @return true if the table is complete
 */
static inline bool upipe_ts_eitd_table_section(struct uref **sections,
                                               struct uref *uref)
{
    uint8_t buffer[EIT_HEADER_SIZE];
    const uint8_t *section_header = uref_block_peek(uref, 0, EIT_HEADER_SIZE,
                                                    buffer);
    if (unlikely(section_header == NULL)) {
        uref_free(uref);
        return false;
    }
    uint8_t section = psi_get_section(section_header);
    uint8_t last_section = psi_get_lastsection(section_header);
    uint8_t version = psi_get_version(section_header);
    uint16_t tableidext = psi_get_tableidext(section_header);
    int err = uref_block_peek_unmap(uref, 0, buffer, section_header);
    ubase_assert(err);

    uref_free(sections[section]);
    sections[section] = uref;

    if (sections[last_section] == NULL)
        return false;

    int i;
    int last_segment = -1;
    for (i = 0; i <= last_section; i++) {
        if (sections[i] == NULL)
            return false;

        section_header = uref_block_peek(sections[i], 0,
                                         EIT_HEADER_SIZE, buffer);
        uint8_t sec_last_section = psi_get_lastsection(section_header);
        uint8_t sec_version = psi_get_version(section_header);
        uint16_t sec_tableidext = psi_get_tableidext(section_header);
        uint8_t sec_last_segment =
            eit_get_segment_last_sec_number(section_header);
        err = uref_block_peek_unmap(sections[i], 0, buffer, section_header);
        ubase_assert(err);

        if (sec_last_section != last_section || sec_version != version ||
            sec_tableidext != tableidext ||
            (last_segment != -1 && sec_last_segment != last_segment))
            return false;
        last_segment = sec_last_segment;

        if (last_segment == i) {
            last_segment = -1;
            while (i + 1 <= last_section && sections[i + 1] == NULL)
                i++;
        }
    }

    /* free spurious, invalid sections */
    for ( ; i < PSI_TABLE_MAX_SECTIONS; i++) {
        uref_free(sections[i]);
        sections[i] = NULL;
    }

    /* a new, full table is available */
    return true;
}

/** @internal @This validates the next EIT.
 *
 * @param upipe description structure of the pipe
 * @return false if the EIT is invalid
 */
static bool upipe_ts_eitd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_eitd *upipe_ts_eitd = upipe_ts_eitd_from_upipe(upipe);
    bool first = true;
    uint16_t onid = 0, tsid = 0;
    upipe_ts_psid_table_foreach (upipe_ts_eitd->next_eit, section_uref) {
        if (section_uref == NULL)
            continue;
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (!eit_validate(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        if (first) {
            first = false;
            onid = eit_get_onid(section);
            tsid = eit_get_tsid(section);
        } else if (eit_get_onid(section) != onid ||
                   eit_get_tsid(section) != tsid) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This is a helper function to parse descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param event event number
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_eitd_parse_descs(struct upipe *upipe,
                                      struct uref *flow_def, uint64_t event,
                                      const uint8_t *descl, uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool valid = true;
        bool copy = false;
        switch (desc_get_tag(desc)) {
            /* DVB */
            case 0x4d: /* Short event descriptor */
                if ((valid = desc4d_validate(desc))) {
                    char code[4];
                    memcpy(code, desc4d_get_lang(desc), 3);
                    code[3] = '\0';
                    UBASE_FATAL(upipe, uref_event_set_language(flow_def,
                                                               code, event))

                    uint8_t event_name_length, text_length;
                    const uint8_t *event_name =
                        desc4d_get_event_name(desc, &event_name_length);
                    const uint8_t *text =
                        desc4d_get_text(desc, &text_length);
                    char *event_name_str =
                        dvb_string_get(event_name, event_name_length,
                                       upipe_ts_eitd_iconv_wrapper, upipe);
                    char *text_str =
                        dvb_string_get(text, text_length,
                                       upipe_ts_eitd_iconv_wrapper, upipe);

                    UBASE_FATAL(upipe, uref_event_set_name(flow_def,
                                event_name_str, event))
                    UBASE_FATAL(upipe, uref_event_set_description(flow_def,
                                text_str, event))
                    free(event_name_str);
                    free(text_str);
                }
                break;

            default:
                copy = true;
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
        if (copy) {
            UBASE_FATAL(upipe, uref_ts_event_add_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE, event))
        }
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_eitd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_eitd *upipe_ts_eitd = upipe_ts_eitd_from_upipe(upipe);
    assert(upipe_ts_eitd->flow_def_input != NULL);

    if (!upipe_ts_eitd_table_section(upipe_ts_eitd->next_eit, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_eitd->eit) &&
        upipe_ts_psid_table_compare(upipe_ts_eitd->eit,
                                    upipe_ts_eitd->next_eit)) {
        /* Identical EIT. */
        upipe_ts_psid_table_clean(upipe_ts_eitd->next_eit);
        upipe_ts_psid_table_init(upipe_ts_eitd->next_eit);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_eitd->next_eit,
                                               upipe_ts_eitd->ubuf_mgr)) ||
        !upipe_ts_eitd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid EIT section received");
        upipe_ts_psid_table_clean(upipe_ts_eitd->next_eit);
        upipe_ts_psid_table_init(upipe_ts_eitd->next_eit);
        return;
    }

    struct uref *flow_def = upipe_ts_eitd_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    bool first = true;
    uint64_t event = 0;
    upipe_ts_psid_table_foreach (upipe_ts_eitd->next_eit, section_uref) {
        if (section_uref == NULL)
            continue; /* legal in EIT */
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        if (first) {
            first = false;
            uint16_t sid = psi_get_tableidext(section);
            uint16_t tsid = eit_get_tsid(section);
            uint16_t onid = eit_get_onid(section);
            uint16_t last_tid = eit_get_last_table_id(section);

            UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
            UBASE_FATAL(upipe, uref_flow_set_id(flow_def, sid))
            UBASE_FATAL(upipe, uref_ts_flow_set_tsid(flow_def, tsid))
            UBASE_FATAL(upipe, uref_ts_flow_set_onid(flow_def, onid))
            UBASE_FATAL(upipe, uref_ts_flow_set_last_table_id(flow_def,
                                                              last_tid))
        }

        const uint8_t *eit_event;
        int j = 0;
        while ((eit_event = eit_get_event((uint8_t *)section, j)) != NULL) {
            j++;

            UBASE_FATAL(upipe, uref_event_set_id(flow_def,
                        eitn_get_event_id(eit_event), event))
            time_t start = dvb_time_decode_UTC(eitn_get_start_time(eit_event));
            UBASE_FATAL(upipe, uref_event_set_start(flow_def,
                        start * UCLOCK_FREQ, event))
            int duration, hour, min, sec;
            dvb_time_decode_bcd(eitn_get_duration_bcd(eit_event), &duration,
                                &hour, &min, &sec);
            UBASE_FATAL(upipe, uref_event_set_duration(flow_def,
                        duration * UCLOCK_FREQ, event))
            UBASE_FATAL(upipe, uref_ts_event_set_running_status(flow_def,
                        eitn_get_running(eit_event), event))
            if (eitn_get_ca(eit_event)) {
                UBASE_FATAL(upipe, uref_ts_event_set_scrambled(flow_def, event))
            }
            upipe_ts_eitd_parse_descs(upipe, flow_def, event,
                    descs_get_desc(eitn_get_descs((uint8_t *)eit_event), 0),
                    eitn_get_desclength(eit_event));

            event++;
        }

        uref_block_unmap(section_uref, 0);
    }

    UBASE_FATAL(upipe, uref_event_set_events(flow_def, event))

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_eitd->eit))
        upipe_ts_psid_table_clean(upipe_ts_eitd->eit);
    upipe_ts_psid_table_copy(upipe_ts_eitd->eit, upipe_ts_eitd->next_eit);
    upipe_ts_psid_table_init(upipe_ts_eitd->next_eit);

    flow_def = upipe_ts_eitd_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    upipe_ts_eitd_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_eitd_output(upipe, NULL, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_eitd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_eitd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_eitd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_eitd_output(upipe, NULL, NULL);
        }
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_eitd_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_eitd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_eitd_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_eitd_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_eitd_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_eitd_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_eitd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_eitd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_eitd_set_output(upipe, output);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_eitd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_eitd *upipe_ts_eitd = upipe_ts_eitd_from_upipe(upipe);
    upipe_ts_psid_table_clean(upipe_ts_eitd->eit);
    upipe_ts_psid_table_clean(upipe_ts_eitd->next_eit);
    upipe_ts_eitd_clean_output(upipe);
    upipe_ts_eitd_clean_ubuf_mgr(upipe);
    upipe_ts_eitd_clean_flow_def(upipe);
    upipe_ts_eitd_clean_iconv(upipe);
    upipe_ts_eitd_clean_urefcount(upipe);
    upipe_ts_eitd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_eitd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_EITD_SIGNATURE,

    .upipe_alloc = upipe_ts_eitd_alloc,
    .upipe_input = upipe_ts_eitd_input,
    .upipe_control = upipe_ts_eitd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_eitd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_eitd_mgr_alloc(void)
{
    return &upipe_ts_eitd_mgr;
}

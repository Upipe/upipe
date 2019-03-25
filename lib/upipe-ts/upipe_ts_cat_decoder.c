/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * This ts is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This ts is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this ts; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the conditional access table
 * Normative references:
 *   EBU TECH 3292-s1
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
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
#include <upipe-ts/upipe_ts_cat_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <bitstream/mpeg/psi.h>
#include <bitstream/mpeg/psi/desc_09.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtscat."

/** @hidden */
static int upipe_ts_catd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_catd pipe. */
struct upipe_ts_catd {
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

    /** currently in effect CAT table */
    UPIPE_TS_PSID_TABLE_DECLARE(cat);
    /** CAT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_cat);

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_catd, upipe, UPIPE_TS_CATD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_catd, urefcount, upipe_ts_catd_free)
UPIPE_HELPER_VOID(upipe_ts_catd)
UPIPE_HELPER_OUTPUT(upipe_ts_catd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_catd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_catd_check,
                      upipe_ts_catd_register_output_request,
                      upipe_ts_catd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_catd, flow_def_input, flow_def_attr)

/** @internal @This allocates a ts_catd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_catd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_catd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_catd *upipe_ts_catd = upipe_ts_catd_from_upipe(upipe);
    upipe_ts_catd_init_urefcount(upipe);
    upipe_ts_catd_init_output(upipe);
    upipe_ts_catd_init_ubuf_mgr(upipe);
    upipe_ts_catd_init_flow_def(upipe);
    upipe_ts_psid_table_init(upipe_ts_catd->cat);
    upipe_ts_psid_table_init(upipe_ts_catd->next_cat);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This validates the next CAT.
 *
 * @param upipe description structure of the pipe
 * @return false if the CAT is invalid
 */
static bool upipe_ts_catd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_catd *upipe_ts_catd = upipe_ts_catd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_catd->next_cat, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (!cat_validate(section) || !psi_check_crc(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This is a helper function to parse biss-ca descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_catd_parse_bissca_descs(struct upipe *upipe,
                                      struct uref *flow_def,
                                      const uint8_t *descl, uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;
    uint8_t esid_n = 0;
    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool valid = true;
        uint16_t length;
        switch (desc_get_tag(desc)) {
            case 0x80: /* BISS-CA entitlement session descriptor */
                length = desc_get_length(desc);
                for (int i = 0; i < length; i += 4) {
                    uint16_t esid = (desc[DESC_HEADER_SIZE + i + 0] << 8) |
                        desc[DESC_HEADER_SIZE + i + 1];
                    uint16_t onid = (desc[DESC_HEADER_SIZE + i + 2] << 8) |
                        desc[DESC_HEADER_SIZE + i + 3];

                    uref_ts_flow_set_cat_onid(flow_def, onid, esid_n);
                    uref_ts_flow_set_cat_esid(flow_def, esid, esid_n);
                    esid_n++;
                }
                break;
            default:
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
    }

    uref_ts_flow_set_cat_esid_n(flow_def, esid_n);
}

/** @internal @This is a helper function to parse descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_catd_parse_descs(struct upipe *upipe,
                                      struct uref *flow_def,
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
            case 0x09:
                valid = desc09_validate(desc);
                if (valid) {
                    uref_ts_flow_set_capid(flow_def, desc09_get_pid(desc));
                    uint16_t sysid = desc09_get_sysid(desc);
                    uref_ts_flow_set_sysid(flow_def, sysid);
                    switch (sysid) {
                        case 0x2610:
                            upipe_ts_catd_parse_bissca_descs(upipe, flow_def,
                                    &desc[DESC09_HEADER_SIZE],
                                    desclength - DESC09_HEADER_SIZE);
                            break;
                        default:
                            upipe_warn_va(upipe, "Unknown CA system 0x%04x",
                                    sysid);
                            break;
                    }
                }
                break;
            default:
                copy = true;
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_cat_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_catd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_catd *upipe_ts_catd = upipe_ts_catd_from_upipe(upipe);
    assert(upipe_ts_catd->flow_def_input != NULL);

    if (!upipe_ts_psid_table_section(upipe_ts_catd->next_cat, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_catd->cat) &&
        upipe_ts_psid_table_compare(upipe_ts_catd->cat,
                                    upipe_ts_catd->next_cat)) {
        /* Identical CAT. */
        upipe_ts_psid_table_clean(upipe_ts_catd->next_cat);
        upipe_ts_psid_table_init(upipe_ts_catd->next_cat);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_catd->next_cat,
                                               upipe_ts_catd->ubuf_mgr)) ||
        !upipe_ts_catd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid CAT section received");
        upipe_ts_psid_table_clean(upipe_ts_catd->next_cat);
        upipe_ts_psid_table_init(upipe_ts_catd->next_cat);
        return;
    }

    struct uref *flow_def = upipe_ts_catd_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))

    upipe_ts_psid_table_foreach (upipe_ts_catd->next_cat, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        upipe_ts_catd_parse_descs(upipe, flow_def,
                cat_get_descl_const(section), cat_get_desclength(section));

        uref_block_unmap(section_uref, 0);
    }

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_catd->cat))
        upipe_ts_psid_table_clean(upipe_ts_catd->cat);
    upipe_ts_psid_table_copy(upipe_ts_catd->cat, upipe_ts_catd->next_cat);
    upipe_ts_psid_table_init(upipe_ts_catd->next_cat);

    flow_def = upipe_ts_catd_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    upipe_ts_catd_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_catd_output(upipe, NULL, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_catd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_catd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_catd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_catd_output(upipe, NULL, NULL);
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
static int upipe_ts_catd_set_flow_def(struct upipe *upipe,
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
    upipe_ts_catd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_catd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_catd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_catd_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_catd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_catd *upipe_ts_catd = upipe_ts_catd_from_upipe(upipe);
    upipe_ts_psid_table_clean(upipe_ts_catd->cat);
    upipe_ts_psid_table_clean(upipe_ts_catd->next_cat);
    upipe_ts_catd_clean_output(upipe);
    upipe_ts_catd_clean_ubuf_mgr(upipe);
    upipe_ts_catd_clean_flow_def(upipe);
    upipe_ts_catd_clean_urefcount(upipe);
    upipe_ts_catd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_catd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_CATD_SIGNATURE,

    .upipe_alloc = upipe_ts_catd_alloc,
    .upipe_input = upipe_ts_catd_input,
    .upipe_control = upipe_ts_catd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_catd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_catd_mgr_alloc(void)
{
    return &upipe_ts_catd_mgr;
}

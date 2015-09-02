/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short Upipe module decoding the network information table of DVB streams
 * Normative references:
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
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
#include <upipe/upipe_helper_iconv.h>
#include <upipe-ts/upipe_ts_nit_decoder.h>
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
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtsnit."
/** we only store UTF-8 */
#define NATIVE_ENCODING "UTF-8"

/** @hidden */
static int upipe_ts_nitd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_nitd pipe. */
struct upipe_ts_nitd {
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

    /** currently in effect NIT table */
    UPIPE_TS_PSID_TABLE_DECLARE(nit);
    /** NIT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_nit);

    /** encoding of the following iconv handle */
    const char *current_encoding;
    /** iconv handle */
    iconv_t iconv_handle;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_nitd, upipe, UPIPE_TS_NITD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_nitd, urefcount, upipe_ts_nitd_free)
UPIPE_HELPER_VOID(upipe_ts_nitd)
UPIPE_HELPER_OUTPUT(upipe_ts_nitd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_nitd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_nitd_check,
                      upipe_ts_nitd_register_output_request,
                      upipe_ts_nitd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_nitd, flow_def_input, flow_def_attr)
UPIPE_HELPER_ICONV(upipe_ts_nitd, NATIVE_ENCODING, current_encoding, iconv_handle)

/** @internal @This allocates a ts_nitd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_nitd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_nitd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_nitd *upipe_ts_nitd = upipe_ts_nitd_from_upipe(upipe);
    upipe_ts_nitd_init_urefcount(upipe);
    upipe_ts_nitd_init_output(upipe);
    upipe_ts_nitd_init_ubuf_mgr(upipe);
    upipe_ts_nitd_init_flow_def(upipe);
    upipe_ts_nitd_init_iconv(upipe);
    upipe_ts_psid_table_init(upipe_ts_nitd->nit);
    upipe_ts_psid_table_init(upipe_ts_nitd->next_nit);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This checks if the ts is already in the table with different
 * parameters.
 *
 * @param upipe description structure of the pipe
 * @param wanted_ts ts to check
 * @return true if there is no different instance of the ts in the table
 */
static bool upipe_ts_nitd_table_compare_ts(struct upipe *upipe,
                                                const uint8_t *wanted_ts)
{
    struct upipe_ts_nitd *upipe_ts_nitd = upipe_ts_nitd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_nitd->next_nit, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        const uint8_t *ts;
        int j = 0;
        while ((ts = nit_get_ts((uint8_t *)section, j)) != NULL) {
            j++;
            if (nitn_get_tsid(ts) == nitn_get_tsid(wanted_ts)) {
                bool result = (ts == wanted_ts) ||
                    (nitn_get_desclength(ts) ==
                     nitn_get_desclength(wanted_ts) &&
                     !memcmp(ts, wanted_ts,
                             NIT_TS_SIZE + nitn_get_desclength(ts)));
                uref_block_unmap(section_uref, 0);
                return result;
            }
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This validates the next NIT.
 *
 * @param upipe description structure of the pipe
 * @return false if the NIT is invalid
 */
static bool upipe_ts_nitd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_nitd *upipe_ts_nitd = upipe_ts_nitd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_nitd->next_nit, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (!nit_validate(section) || !psi_check_crc(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        const uint8_t *ts;
        int j = 0;
        while ((ts = nit_get_ts((uint8_t *)section, j)) != NULL) {
            j++;
            /* check that the ts is not already in the table */
            if (!upipe_ts_nitd_table_compare_ts(upipe, ts)) {
                uref_block_unmap(section_uref, 0);
                return false;
            }
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
 * @param descl pointer to descriptor list
 * @param desclength length of the decriptor list
 * @return an error code
 */
static void upipe_ts_nitd_parse_descs(struct upipe *upipe,
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
            /* DVB */
            case 0x40: /* network_name_descriptor */
                if ((valid = desc40_validate(desc))) {
                    uint8_t networkname_length;
                    const uint8_t *networkname =
                        desc40_get_networkname(desc, &networkname_length);
                    char *networkname_string =
                        dvb_string_get(networkname, networkname_length,
                                       upipe_ts_nitd_iconv_wrapper, upipe);
                    UBASE_FATAL(upipe, uref_ts_flow_set_network_name(flow_def,
                                networkname_string))
                    free(networkname_string);
                }
                break;

            default:
                copy = true;
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_nit_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }
}

/** @internal @This is a helper function to parse ts descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param event event number
 * @param descl pointer to descriptor list
 * @param desclength length of the decriptor list
 * @return an error code
 */
static void upipe_ts_nitd_parse_ts_descs(struct upipe *upipe,
        struct uref *flow_def, uint64_t ts_number,
        const uint8_t *descl, uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool copy = false;
        switch (desc_get_tag(desc)) {
            default:
                copy = true;
                break;
        }

        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_nit_ts_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE,
                        ts_number))
        }
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_nitd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_nitd *upipe_ts_nitd = upipe_ts_nitd_from_upipe(upipe);
    assert(upipe_ts_nitd->flow_def_input != NULL);

    if (!upipe_ts_psid_table_section(upipe_ts_nitd->next_nit, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_nitd->nit) &&
        upipe_ts_psid_table_compare(upipe_ts_nitd->nit,
                                    upipe_ts_nitd->next_nit)) {
        /* Identical NIT. */
        upipe_ts_psid_table_clean(upipe_ts_nitd->next_nit);
        upipe_ts_psid_table_init(upipe_ts_nitd->next_nit);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_nitd->next_nit,
                                               upipe_ts_nitd->ubuf_mgr)) ||
        !upipe_ts_nitd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid NIT section received");
        upipe_ts_psid_table_clean(upipe_ts_nitd->next_nit);
        upipe_ts_psid_table_init(upipe_ts_nitd->next_nit);
        return;
    }

    struct uref *flow_def = upipe_ts_nitd_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))

    bool first = true;
    uint64_t ts_number = 0;
    upipe_ts_psid_table_foreach (upipe_ts_nitd->next_nit, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        if (first) {
            first = false;
            uint16_t nid = nit_get_nid(section);

            UBASE_FATAL(upipe, uref_ts_flow_set_nid(flow_def, nid))
        }

        upipe_ts_nitd_parse_descs(upipe, flow_def,
                descs_get_desc(nit_get_descs((uint8_t *)section), 0),
                nit_get_desclength(section));

        const uint8_t *ts;
        int j = 0;
        while ((ts = nit_get_ts((uint8_t *)section, j)) != NULL) {
            j++;

            UBASE_FATAL(upipe, uref_ts_flow_set_nit_ts_tsid(flow_def,
                        nitn_get_tsid(ts), ts_number))
            UBASE_FATAL(upipe, uref_ts_flow_set_nit_ts_onid(flow_def,
                        nitn_get_onid(ts), ts_number))
            upipe_ts_nitd_parse_ts_descs(upipe, flow_def, ts_number,
                    descs_get_desc(nitn_get_descs((uint8_t *)ts), 0),
                    nitn_get_desclength(ts));

            ts_number++;
        }

        uref_block_unmap(section_uref, 0);
    }

    UBASE_FATAL(upipe, uref_ts_flow_set_nit_ts(flow_def, ts_number))

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_nitd->nit))
        upipe_ts_psid_table_clean(upipe_ts_nitd->nit);
    upipe_ts_psid_table_copy(upipe_ts_nitd->nit, upipe_ts_nitd->next_nit);
    upipe_ts_psid_table_init(upipe_ts_nitd->next_nit);

    flow_def = upipe_ts_nitd_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    upipe_ts_nitd_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_nitd_output(upipe, NULL, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_nitd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_nitd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_nitd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_nitd_output(upipe, NULL, NULL);
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
static int upipe_ts_nitd_set_flow_def(struct upipe *upipe,
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
    upipe_ts_nitd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_nitd_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_nitd_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_nitd_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_nitd_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_nitd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_nitd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_nitd_set_output(upipe, output);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_nitd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_nitd *upipe_ts_nitd = upipe_ts_nitd_from_upipe(upipe);
    upipe_ts_psid_table_clean(upipe_ts_nitd->nit);
    upipe_ts_psid_table_clean(upipe_ts_nitd->next_nit);
    upipe_ts_nitd_clean_output(upipe);
    upipe_ts_nitd_clean_ubuf_mgr(upipe);
    upipe_ts_nitd_clean_flow_def(upipe);
    upipe_ts_nitd_clean_iconv(upipe);
    upipe_ts_nitd_clean_urefcount(upipe);
    upipe_ts_nitd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_nitd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_NITD_SIGNATURE,

    .upipe_alloc = upipe_ts_nitd_alloc,
    .upipe_input = upipe_ts_nitd_input,
    .upipe_control = upipe_ts_nitd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_nitd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_nitd_mgr_alloc(void)
{
    return &upipe_ts_nitd_mgr;
}

/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This service is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This service is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this service; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the service description table of DVB streams
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
#include <upipe-ts/upipe_ts_sdt_decoder.h>
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
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtssdt."
/** we only store UTF-8 */
#define NATIVE_ENCODING "UTF-8"

/** @hidden */
static int upipe_ts_sdtd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_sdtd pipe. */
struct upipe_ts_sdtd {
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

    /** currently in effect SDT table */
    UPIPE_TS_PSID_TABLE_DECLARE(sdt);
    /** SDT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_sdt);
    /** current TSID */
    int tsid;
    /** current original network ID */
    int onid;
    /** list of services */
    struct uchain services;

    /** encoding of the following iconv handle */
    const char *current_encoding;
    /** iconv handle */
    iconv_t iconv_handle;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_sdtd, upipe, UPIPE_TS_SDTD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_sdtd, urefcount, upipe_ts_sdtd_free)
UPIPE_HELPER_VOID(upipe_ts_sdtd)
UPIPE_HELPER_OUTPUT(upipe_ts_sdtd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_sdtd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_sdtd_check,
                      upipe_ts_sdtd_register_output_request,
                      upipe_ts_sdtd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_sdtd, flow_def_input, flow_def_attr)
UPIPE_HELPER_ICONV(upipe_ts_sdtd, NATIVE_ENCODING, current_encoding, iconv_handle)

/** @internal @This allocates a ts_sdtd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_sdtd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_sdtd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    upipe_ts_sdtd_init_urefcount(upipe);
    upipe_ts_sdtd_init_output(upipe);
    upipe_ts_sdtd_init_ubuf_mgr(upipe);
    upipe_ts_sdtd_init_flow_def(upipe);
    upipe_ts_sdtd_init_iconv(upipe);
    upipe_ts_psid_table_init(upipe_ts_sdtd->sdt);
    upipe_ts_psid_table_init(upipe_ts_sdtd->next_sdt);
    upipe_ts_sdtd->tsid = upipe_ts_sdtd->onid = -1;
    ulist_init(&upipe_ts_sdtd->services);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This cleans up the list of services.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sdtd_clean_services(struct upipe *upipe)
{
    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_sdtd->services, uchain, uchain_tmp) {
        struct uref *flow_def = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(flow_def);
    }
}

/** @internal @This checks if the service is already in the table with different
 * parameters.
 *
 * @param upipe description structure of the pipe
 * @param wanted_service service to check
 * @return true if there is no different instance of the service in the table
 */
static bool upipe_ts_sdtd_table_compare_service(struct upipe *upipe,
                                                const uint8_t *wanted_service)
{
    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_sdtd->next_sdt, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        const uint8_t *service;
        int j = 0;
        while ((service = sdt_get_service((uint8_t *)section, j)) != NULL) {
            j++;
            if (sdtn_get_sid(service) == sdtn_get_sid(wanted_service)) {
                bool result = (service == wanted_service) ||
                    (sdtn_get_desclength(service) ==
                     sdtn_get_desclength(wanted_service) &&
                     !memcmp(service, wanted_service,
                             SDT_SERVICE_SIZE + sdtn_get_desclength(service)));
                uref_block_unmap(section_uref, 0);
                return result;
            }
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This validates the next SDT.
 *
 * @param upipe description structure of the pipe
 * @return false if the SDT is invalid
 */
static bool upipe_ts_sdtd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    bool first = true;
    uint16_t onid = 0;
    upipe_ts_psid_table_foreach (upipe_ts_sdtd->next_sdt, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (!sdt_validate(section) || !psi_check_crc(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        if (first) {
            first = false;
            onid = sdt_get_onid(section);
        } else if (sdt_get_onid(section) != onid) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        const uint8_t *service;
        int j = 0;
        while ((service = sdt_get_service((uint8_t *)section, j)) != NULL) {
            j++;
            /* check that the service is not already in the table */
            if (!upipe_ts_sdtd_table_compare_service(upipe, service)) {
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
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_sdtd_parse_descs(struct upipe *upipe,
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
            case 0x48: /* service descriptor */
                if ((valid = desc48_validate(desc))) {
                    uint8_t provider_length, service_length;
                    const uint8_t *provider =
                        desc48_get_provider(desc, &provider_length);
                    const uint8_t *service =
                        desc48_get_service(desc, &service_length);
                    char *provider_string =
                        dvb_string_get(provider, provider_length,
                                       upipe_ts_sdtd_iconv_wrapper, upipe);
                    char *service_string =
                        dvb_string_get(service, service_length,
                                       upipe_ts_sdtd_iconv_wrapper, upipe);
                    UBASE_FATAL(upipe, uref_flow_set_name(flow_def,
                                service_string))
                    UBASE_FATAL(upipe, uref_ts_flow_set_provider_name(flow_def,
                                provider_string))
                    free(provider_string);
                    free(service_string);

                    UBASE_FATAL(upipe, uref_ts_flow_set_service_type(flow_def,
                                desc48_get_type(desc)))
                }
                break;

            default:
                copy = true;
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_sdt_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }
}

/** @internal @This builds the flow definition corresponding to a service.
 *
 * @param upipe description structure of the pipe
 * @param service service structure in SDT
 */
static void upipe_ts_sdtd_build_service(struct upipe *upipe,
                                        const uint8_t *service)
{
    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    struct uref *flow_def = uref_dup(upipe_ts_sdtd->flow_def_input);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint16_t sid = sdtn_get_sid(service);
    bool eitschedule = sdtn_get_eitschedule(service);
    bool eitpresent = sdtn_get_eitpresent(service);
    uint8_t running = sdtn_get_running(service);
    bool ca = sdtn_get_ca(service);

    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
    UBASE_FATAL(upipe, uref_flow_set_id(flow_def, sid))
    if (eitpresent) {
        UBASE_FATAL(upipe, uref_ts_flow_set_eit(flow_def))
        if (eitschedule) {
            UBASE_FATAL(upipe, uref_ts_flow_set_eit_schedule(flow_def))
        }
    }
    UBASE_FATAL(upipe, uref_ts_flow_set_running_status(flow_def, running))
    if (ca) {
        UBASE_FATAL(upipe, uref_ts_flow_set_scrambled(flow_def))
    }
    upipe_ts_sdtd_parse_descs(upipe, flow_def,
            descs_get_desc(sdtn_get_descs((uint8_t *)service), 0),
            sdtn_get_desclength(service));
    ulist_add(&upipe_ts_sdtd->services, uref_to_uchain(flow_def));
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_sdtd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    assert(upipe_ts_sdtd->flow_def_input != NULL);

    if (!upipe_ts_psid_table_section(upipe_ts_sdtd->next_sdt, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_sdtd->sdt) &&
        upipe_ts_psid_table_compare(upipe_ts_sdtd->sdt,
                                    upipe_ts_sdtd->next_sdt)) {
        /* Identical SDT. */
        upipe_ts_psid_table_clean(upipe_ts_sdtd->next_sdt);
        upipe_ts_psid_table_init(upipe_ts_sdtd->next_sdt);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_sdtd->next_sdt,
                                               upipe_ts_sdtd->ubuf_mgr)) ||
        !upipe_ts_sdtd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid SDT section received");
        upipe_ts_psid_table_clean(upipe_ts_sdtd->next_sdt);
        upipe_ts_psid_table_init(upipe_ts_sdtd->next_sdt);
        return;
    }

    upipe_ts_sdtd_clean_services(upipe);

    bool first = true;
    upipe_ts_psid_table_foreach (upipe_ts_sdtd->next_sdt, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        if (first) {
            first = false;
            uint16_t tsid = sdt_get_tsid(section);
            uint16_t onid = sdt_get_onid(section);

            if (unlikely(tsid != upipe_ts_sdtd->tsid ||
                         onid != upipe_ts_sdtd->onid)) {
                struct uref *flow_def =
                    upipe_ts_sdtd_alloc_flow_def_attr(upipe);
                if (unlikely(flow_def == NULL)) {
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                    return;
                }
                upipe_ts_sdtd->tsid = tsid;
                upipe_ts_sdtd->onid = onid;
                UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
                UBASE_FATAL(upipe, uref_flow_set_id(flow_def, tsid))
                UBASE_FATAL(upipe, uref_ts_flow_set_onid(flow_def, onid))
                flow_def = upipe_ts_sdtd_store_flow_def_attr(upipe, flow_def);
                if (unlikely(flow_def == NULL)) {
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                    return;
                }
                upipe_ts_sdtd_store_flow_def(upipe, flow_def);
                /* Force sending flow def */
                upipe_ts_sdtd_output(upipe, NULL, upump_p);
            }
        }

        const uint8_t *service;
        int j = 0;
        while ((service = sdt_get_service((uint8_t *)section, j)) != NULL) {
            j++;
            upipe_ts_sdtd_build_service(upipe, service);
        }

        uref_block_unmap(section_uref, 0);
    }

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_sdtd->sdt))
        upipe_ts_psid_table_clean(upipe_ts_sdtd->sdt);
    upipe_ts_psid_table_copy(upipe_ts_sdtd->sdt, upipe_ts_sdtd->next_sdt);
    upipe_ts_psid_table_init(upipe_ts_sdtd->next_sdt);

    upipe_split_throw_update(upipe);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_sdtd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_sdtd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_sdtd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_sdtd_output(upipe, NULL, NULL);
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
static int upipe_ts_sdtd_set_flow_def(struct upipe *upipe,
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
    upipe_ts_sdtd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This iterates over flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return an error code
 */
static int upipe_ts_sdtd_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    assert(p != NULL);
    struct uchain *uchain;
    if (*p != NULL)
        uchain = uref_to_uchain(*p);
    else
        uchain = &upipe_ts_sdtd->services;
    if (ulist_is_last(&upipe_ts_sdtd->services, uchain)) {
        *p = NULL;
        return UBASE_ERR_NONE;
    }
    *p = uref_from_uchain(uchain->next);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_sdtd_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_sdtd_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_sdtd_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_sdtd_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_sdtd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sdtd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_sdtd_set_output(upipe, output);
        }
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_sdtd_iterate(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sdtd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_sdtd *upipe_ts_sdtd = upipe_ts_sdtd_from_upipe(upipe);
    upipe_ts_psid_table_clean(upipe_ts_sdtd->sdt);
    upipe_ts_psid_table_clean(upipe_ts_sdtd->next_sdt);
    upipe_ts_sdtd_clean_services(upipe);
    upipe_ts_sdtd_clean_output(upipe);
    upipe_ts_sdtd_clean_ubuf_mgr(upipe);
    upipe_ts_sdtd_clean_flow_def(upipe);
    upipe_ts_sdtd_clean_iconv(upipe);
    upipe_ts_sdtd_clean_urefcount(upipe);
    upipe_ts_sdtd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_sdtd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SDTD_SIGNATURE,

    .upipe_alloc = upipe_ts_sdtd_alloc,
    .upipe_input = upipe_ts_sdtd_input,
    .upipe_control = upipe_ts_sdtd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_sdtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sdtd_mgr_alloc(void)
{
    return &upipe_ts_sdtd_mgr;
}

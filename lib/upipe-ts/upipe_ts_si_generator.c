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
 * @short Upipe module generating DVB SI tables
 * Normative references:
 *  - ISO/IEC 13818-1:2007(E) (MPEG-2 Systems)
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 */

#include <upipe/uclock.h>
#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_event.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_si_generator.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/uref_ts_event.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** T-STD TB octet rate for PSI tables */
#define TB_RATE_PSI 125000
/** DVB minimum section interval */
#define MIN_SECTION_INTERVAL (UCLOCK_FREQ / 40)
/** default network ID */
#define DEFAULT_NID 65535
/** default transport stream ID */
#define DEFAULT_TSID 65535
/** default name */
#define DEFAULT_NAME "Upipe"
/** we only store UTF-8 at the moment */
#define NATIVE_ENCODING "UTF-8"

/** @internal @This is the private context of a ts sig subpipe outputting a
 * table. */
struct upipe_ts_sig_output {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** output octetrate */
    uint64_t octetrate;
    /** cr_sys of the last transmitted packet */
    uint64_t cr_sys;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_sig_output, upipe, UPIPE_TS_SIG_OUTPUT_SIGNATURE)
UPIPE_HELPER_OUTPUT(upipe_ts_sig_output, output, flow_def, output_state,
                    request_list)

/** @internal @This is the private context of a ts sig pipe. */
struct upipe_ts_sig {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** uclock structure (used for TDT) */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** pipe acting as pseudo-output (for requests) */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *output_flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** input flow definition packet */
    struct uref *flow_def;
    /** true if the update was frozen */
    bool frozen;

    /** NIT version number */
    uint8_t nit_version;
    /** NIT sections */
    struct uchain nit_sections;
    /** number of NIT sections */
    uint8_t nit_nb_sections;
    /** size of NIT sections */
    uint64_t nit_size;
    /** NIT interval */
    uint64_t nit_interval;
    /** last NIT cr_sys */
    uint64_t nit_cr_sys;
    /** false if a new NIT was built but not sent yet */
    bool nit_sent;

    /** SDT version number */
    uint8_t sdt_version;
    /** SDT sections */
    struct uchain sdt_sections;
    /** number of SDT sections */
    uint8_t sdt_nb_sections;
    /** size of SDT sections */
    uint64_t sdt_size;
    /** SDT interval */
    uint64_t sdt_interval;
    /** last SDT cr_sys */
    uint64_t sdt_cr_sys;
    /** false if a new SDT was built but not sent yet */
    bool sdt_sent;

    /** TDT interval */
    uint64_t tdt_interval;
    /** last TDT cr_sys */
    uint64_t tdt_cr_sys;

    /** NIT output */
    struct upipe_ts_sig_output nit_output;
    /** SDT output */
    struct upipe_ts_sig_output sdt_output;
    /** EIT output */
    struct upipe_ts_sig_output eit_output;
    /** TDT output */
    struct upipe_ts_sig_output tdt_output;
    /** output subpipe manager */
    struct upipe_mgr output_mgr;

    /** list of service subpipes */
    struct uchain services;
    /** manager to create service subpipes */
    struct upipe_mgr service_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_sig, upipe, UPIPE_TS_SIG_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_sig, urefcount, upipe_ts_sig_free)
UPIPE_HELPER_OUTPUT(upipe_ts_sig, output, output_flow_def, output_state,
                    request_list)
UPIPE_HELPER_UREF_MGR(upipe_ts_sig, uref_mgr, uref_mgr_request, NULL,
                      upipe_ts_sig_register_output_request,
                      upipe_ts_sig_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_ts_sig, ubuf_mgr, flow_format, ubuf_mgr_request,
                      NULL, upipe_ts_sig_register_output_request,
                      upipe_ts_sig_unregister_output_request)
UPIPE_HELPER_UCLOCK(upipe_ts_sig, uclock, uclock_request, NULL,
                    upipe_ts_sig_register_output_request,
                    upipe_ts_sig_unregister_output_request)

UBASE_FROM_TO(upipe_ts_sig, upipe_mgr, output_mgr, output_mgr)
UBASE_FROM_TO(upipe_ts_sig, upipe_ts_sig_output, nit_output, nit_output)
UBASE_FROM_TO(upipe_ts_sig, upipe_ts_sig_output, sdt_output, sdt_output)
UBASE_FROM_TO(upipe_ts_sig, upipe_ts_sig_output, eit_output, eit_output)
UBASE_FROM_TO(upipe_ts_sig, upipe_ts_sig_output, tdt_output, tdt_output)

/** @hidden */
static void upipe_ts_sig_build_sdt_flow_def(struct upipe *upipe);
static void upipe_ts_sig_build_sdt(struct upipe *upipe);
static void upipe_ts_sig_build_eit_flow_def(struct upipe *upipe);

/** @internal @This is the private context of a service of a ts_sig pipe
 * (outputs EITp/f). */
struct upipe_ts_sig_service {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** input flow definition packet */
    struct uref *flow_def;

    /** EIT version number */
    uint8_t eit_version;
    /** EIT sections */
    struct uchain eit_sections;
    /** number of EIT sections */
    uint8_t eit_nb_sections;
    /** size of EIT sections */
    uint64_t eit_size;
    /** EIT interval */
    uint64_t eit_interval;
    /** last EIT cr_sys */
    uint64_t eit_cr_sys;
    /** false if a new EIT was built but not sent yet */
    bool eit_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_sig_service, upipe, UPIPE_TS_SIG_SERVICE_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_sig_service, urefcount,
                       upipe_ts_sig_service_free)
UPIPE_HELPER_VOID(upipe_ts_sig_service)

UPIPE_HELPER_SUBPIPE(upipe_ts_sig, upipe_ts_sig_service, service, service_mgr,
                     services, uchain)

/** @internal @This allocates a service of a ts_sig pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_sig_service_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                uint32_t signature,
                                                va_list args)
{
    struct upipe *upipe = upipe_ts_sig_service_alloc_void(mgr, uprobe,
                                                          signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_sig_service *service =
        upipe_ts_sig_service_from_upipe(upipe);
    upipe_ts_sig_service_init_urefcount(upipe);
    upipe_ts_sig_service_init_sub(upipe);
    service->flow_def = NULL;
    service->eit_version = 0;
    ulist_init(&service->eit_sections);
    service->eit_nb_sections = 0;
    service->eit_size = 0;
    service->eit_interval = 0;
    service->eit_cr_sys = 0;
    service->eit_sent = false;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This generates a new EIT PSI section.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_service_build_eit(struct upipe *upipe)
{
    struct upipe_ts_sig_service *service =
        upipe_ts_sig_service_from_upipe(upipe);
    struct upipe_ts_sig *sig =
        upipe_ts_sig_from_service_mgr(upipe->mgr);
    uint64_t sid, tsid, onid;
    if (unlikely(service->flow_def == NULL ||
                 !ubase_check(uref_flow_get_id(service->flow_def, &sid)) ||
                 sig->flow_def == NULL ||
                 !ubase_check(uref_flow_get_id(sig->flow_def, &tsid)) ||
                 !ubase_check(uref_ts_flow_get_onid(sig->flow_def, &onid))))
        return;
    if (!ubase_check(uref_ts_flow_get_eit(service->flow_def))) {
        /* no EIT */
        struct uchain *section_chain;
        while ((section_chain = ulist_pop(&service->eit_sections)) != NULL)
            ubuf_free(ubuf_from_uchain(section_chain));
        return;
    }
    if (unlikely(sig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding an EIT");
        return;
    }


    upipe_notice_va(upipe, "new EIT sid=%"PRIu64" version=%"PRIu8,
                    sid, service->eit_version);

    unsigned int nb_sections = 0;
    uint64_t i = 0;
    uint64_t event_number = 0;
    uref_event_get_events(service->flow_def, &event_number);
    uint64_t total_size = 0;

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&service->eit_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));

    do {
        if (unlikely(nb_sections >= PSI_TABLE_MAX_SECTIONS)) {
            upipe_warn(upipe, "EIT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        struct ubuf *ubuf = ubuf_block_alloc(sig->ubuf_mgr,
                PSI_PRIVATE_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        eit_init(buffer, true);
        /* set length later */
        psi_set_length(buffer, PSI_PRIVATE_MAX_SIZE);
        eit_set_sid(buffer, sid);
        eit_set_tsid(buffer, tsid);
        eit_set_onid(buffer, onid);
        eit_set_last_table_id(buffer, EIT_TABLE_ID_PF_ACTUAL);
        psi_set_version(buffer, service->eit_version);
        psi_set_current(buffer);
        psi_set_section(buffer, nb_sections);
        /* set last section and segment_last_sec_number in the end */

        uint16_t j = 0;
        uint8_t *event;
        while ((event = eit_get_event(buffer, j)) != NULL && i < event_number) {
            if (j) /* DVB only allows 1 event per section for EITp/f */
                break;

            uint64_t event_id;
            uint64_t start, duration;
            uint8_t running;
            if (!ubase_check(uref_event_get_id(service->flow_def,
                            &event_id, i)) ||
                !ubase_check(uref_event_get_start(service->flow_def,
                            &start, i)) ||
                !ubase_check(uref_event_get_duration(service->flow_def,
                            &duration, i)) ||
                !ubase_check(uref_ts_event_get_running_status(service->flow_def,
                            &running, i))) {
                i++;
                continue;
            }
            bool ca =
                ubase_check(uref_ts_event_get_scrambled(service->flow_def, i));
            const char *language, *name_str, *description_str;
            uint8_t *name = NULL, *description = NULL;
            size_t name_size = 0, description_size = 0;
            bool desc4d = 
                ubase_check(uref_event_get_language(service->flow_def,
                            &language, i)) &&
                ubase_check(uref_event_get_name(service->flow_def,
                            &name_str, i)) &&
                ubase_check(uref_event_get_description(service->flow_def,
                            &description_str, i));
            if (desc4d) {
                name = dvb_string_set((const uint8_t *)name_str,
                        strlen(name_str), NATIVE_ENCODING,
                        &name_size);
                description = dvb_string_set((const uint8_t *)description_str,
                        strlen(description_str), NATIVE_ENCODING,
                        &description_size);
            }

            size_t descriptors_size =
                uref_ts_event_size_descriptors(service->flow_def, i);
            if (!eit_validate_event(buffer, event, descriptors_size +
                        (desc4d ? (DESC4D_HEADER_SIZE + name_size + 1 +
                         description_size + 1) : 0))) {
                free(name);
                free(description);
                if (j)
                    break;
                upipe_err_va(upipe, "EIT event too large");
                ubuf_free(ubuf);
                upipe_throw_error(upipe, UBASE_ERR_INVALID);
                return;
            }

            start /= UCLOCK_FREQ;
            duration /= UCLOCK_FREQ;
            upipe_notice_va(upipe,
                    " * event id=%"PRIu64" start=%"PRIu64" duration=%"PRIu64" name=\"%s\"",
                    event_id, start, duration, name_str);

            j++;
            eitn_init(event);
            eitn_set_event_id(event, event_id);
            eitn_set_start_time(event, dvb_time_encode_UTC(start));
            eitn_set_duration_bcd(event, dvb_time_encode_duration(duration));
            eitn_set_running(event, running);
            if (ca)
                eitn_set_ca(event);
            eitn_set_desclength(event, descriptors_size +
                        (desc4d ? (DESC4D_HEADER_SIZE + name_size + 1 +
                                   description_size + 1) : 0));
            uint16_t k = 0;
            if (desc4d) {
                uint8_t *desc = descs_get_desc(eitn_get_descs(event), k++);
                desc4d_init(desc);
                desc4d_set_lang(desc, (const uint8_t *)language);
                desc4d_set_event_name(desc, name, name_size);
                desc4d_set_text(desc, description, description_size);
                desc4d_set_length(desc);
                free(name);
                free(description);
            }
            if (descriptors_size)
                uref_ts_event_extract_descriptors(service->flow_def,
                        descs_get_desc(eitn_get_descs(event), k), i);
            i++;
        }

        eit_set_length(buffer, event - buffer - EIT_HEADER_SIZE);
        uint16_t eit_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);

        ubuf_block_resize(ubuf, 0, eit_size);
        ulist_add(&service->eit_sections, ubuf_to_uchain(ubuf));
        nb_sections++;
        total_size += eit_size;
    } while (i < event_number);

    ulist_foreach (&service->eit_sections, section_chain) {
        struct ubuf *ubuf = ubuf_from_uchain(section_chain);
        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        eit_set_segment_last_sec_number(buffer, nb_sections - 1);
        psi_set_lastsection(buffer, nb_sections - 1);
        psi_set_crc(buffer);

        ubuf_block_unmap(ubuf, 0);
    }

    upipe_notice_va(upipe, "end EIT (%u sections)", nb_sections);

    service->eit_nb_sections = nb_sections;
    service->eit_size = total_size;
    service->eit_sent = false;
}

/** @internal @This compares two services wrt. ascending service IDs.
 *
 * @param uchain1 pointer to first service
 * @param uchain2 pointer to second service
 * @return an integer less than, equal to, or greater than zero if the first
 * argument is considered to be respectively less than, equal to, or greater
 * than the second.
 */
static int upipe_ts_sig_service_compare(struct uchain **uchain1,
                                        struct uchain **uchain2)
{
    struct upipe_ts_sig_service *service1 =
        upipe_ts_sig_service_from_uchain(*uchain1);
    struct upipe_ts_sig_service *service2 =
        upipe_ts_sig_service_from_uchain(*uchain2);
    if (service1->flow_def == NULL && service2->flow_def == NULL)
        return 0;
    if (service1->flow_def == NULL)
        return -1;
    else if (service2->flow_def == NULL)
        return 1;
    return uref_flow_cmp_id(service1->flow_def, service2->flow_def);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_sig_service_set_flow_def(struct upipe *upipe,
                                             struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    uint64_t sid;
    UBASE_RETURN(uref_flow_get_id(flow_def, &sid))

    struct upipe_ts_sig_service *service =
        upipe_ts_sig_service_from_upipe(upipe);
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    bool sdt_change = service->flow_def == NULL ||
        uref_flow_cmp_id(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_service_type(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_eit(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_eit_schedule(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_scrambled(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_running_status(flow_def, service->flow_def) ||
        uref_flow_cmp_name(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_provider_name(flow_def, service->flow_def) ||
        uref_ts_flow_compare_sdt_descriptors(flow_def, service->flow_def);

    bool eit = ubase_check(uref_ts_flow_get_eit(flow_def));
    bool eit_change = service->flow_def == NULL ||
        uref_flow_cmp_id(flow_def, service->flow_def) ||
        uref_ts_flow_cmp_eit(flow_def, service->flow_def);
    if (!eit_change && eit) {
        eit_change = uref_event_cmp_events(flow_def, service->flow_def);
        if (!eit_change) {
            uint64_t event_number = 0;
            uref_event_get_events(flow_def, &event_number);
            for (uint64_t i = 0; !eit_change && i < event_number; i++) {
                eit_change =
                    uref_event_cmp_id(flow_def, service->flow_def, i) ||
                    uref_event_cmp_start(flow_def, service->flow_def, i) ||
                    uref_event_cmp_duration(flow_def, service->flow_def, i) ||
                    uref_event_cmp_language(flow_def, service->flow_def, i) ||
                    uref_event_cmp_name(flow_def, service->flow_def, i) ||
                    uref_event_cmp_description(flow_def,
                            service->flow_def, i) ||
                    uref_ts_event_cmp_running_status(flow_def,
                            service->flow_def, i) ||
                    uref_ts_event_cmp_scrambled(flow_def,
                            service->flow_def, i) ||
                    uref_ts_event_compare_descriptors(flow_def,
                            service->flow_def, i);
            }
        }
    }

    uref_free(service->flow_def);
    service->flow_def = flow_def_dup;

    struct upipe_ts_sig *sig = upipe_ts_sig_from_service_mgr(upipe->mgr);
    ulist_sort(&sig->services, upipe_ts_sig_service_compare);

    if (eit_change) {
        upipe_ts_sig_service_build_eit(upipe);
        upipe_ts_sig_build_eit_flow_def(upipe_ts_sig_to_upipe(sig));
    }

    if (sdt_change) {
        upipe_ts_sig_build_sdt(upipe_ts_sig_to_upipe(sig));
        upipe_ts_sig_build_sdt_flow_def(upipe_ts_sig_to_upipe(sig));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_sig_service_control(struct upipe *upipe,
                                        int command, va_list args)
{
    struct upipe_ts_sig_service *upipe_ts_sig_service =
        upipe_ts_sig_service_from_upipe(upipe);
    struct upipe_ts_sig *sig = upipe_ts_sig_from_service_mgr(upipe->mgr);

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_sig_alloc_output_proxy(upipe_ts_sig_to_upipe(sig),
                    request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_sig_free_output_proxy(upipe_ts_sig_to_upipe(sig),
                    request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_sig_service_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sig_service_get_super(upipe, p);
        }

        case UPIPE_TS_MUX_GET_EIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            *interval_p = upipe_ts_sig_service->eit_interval;
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_SET_EIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            upipe_ts_sig_service->eit_interval = va_arg(args, uint64_t);
            upipe_ts_sig_build_eit_flow_def(upipe_ts_sig_to_upipe(sig));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_service_free(struct upipe *upipe)
{
    struct upipe_ts_sig_service *service =
        upipe_ts_sig_service_from_upipe(upipe);
    struct upipe_ts_sig *sig = upipe_ts_sig_from_service_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_ts_sig_service_clean_sub(upipe);
    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&service->eit_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));
    uref_free(service->flow_def);

    upipe_ts_sig_build_sdt(upipe_ts_sig_to_upipe(sig));
    upipe_ts_sig_build_sdt_flow_def(upipe_ts_sig_to_upipe(sig));

    upipe_ts_sig_service_clean_urefcount(upipe);
    upipe_ts_sig_service_free_void(upipe);
}

/** @internal @This initializes the service manager for a ts_sig pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_init_service_mgr(struct upipe *upipe)
{
    struct upipe_ts_sig *upipe_ts_sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_mgr *service_mgr = &upipe_ts_sig->service_mgr;
    service_mgr->refcount = upipe_ts_sig_to_urefcount(upipe_ts_sig);
    service_mgr->signature = UPIPE_TS_SIG_SERVICE_SIGNATURE;
    service_mgr->upipe_alloc = upipe_ts_sig_service_alloc;
    service_mgr->upipe_input = NULL;
    service_mgr->upipe_control = upipe_ts_sig_service_control;
}

/** @internal @This initializes an output subpipe of a ts_sig pipe.
 *
 * @param upipe pointer to subpipe
 * @param output_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_ts_sig_output_init(struct upipe *upipe,
        struct upipe_mgr *output_mgr, struct uprobe *uprobe)
{
    struct upipe_ts_sig *upipe_ts_sig =
        upipe_ts_sig_from_output_mgr(output_mgr);
    upipe_init(upipe, output_mgr, uprobe);
    upipe->refcount = &upipe_ts_sig->urefcount;

    struct upipe_ts_sig_output *upipe_ts_sig_output =
        upipe_ts_sig_output_from_upipe(upipe);
    upipe_ts_sig_output_init_output(upipe);
    upipe_ts_sig_output->octetrate = 0;
    upipe_ts_sig_output->cr_sys = 0;

    upipe_throw_ready(upipe);
}

/** @internal @This processes control commands on a ts_sig pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_sig_output_control(struct upipe *upipe,
                                       int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_sig_output_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sig_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_sig_output_set_output(upipe, output);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_ts_sig_to_upipe(upipe_ts_sig_from_output_mgr(upipe->mgr));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}


/** @This clean up an output subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_output_clean(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_sig_output_clean_output(upipe);

    upipe_clean(upipe);
}

/** @internal @This initializes the output manager for a blackmagic pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_init_output_mgr(struct upipe *upipe)
{
    struct upipe_ts_sig *upipe_ts_sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_mgr *output_mgr = &upipe_ts_sig->output_mgr;
    output_mgr->refcount = NULL;
    output_mgr->signature = UPIPE_TS_SIG_OUTPUT_SIGNATURE;
    output_mgr->upipe_alloc = NULL;
    output_mgr->upipe_input = NULL;
    output_mgr->upipe_control = upipe_ts_sig_output_control;
    output_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a ts_sig pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *_upipe_ts_sig_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    if (signature != UPIPE_TS_SIG_SIGNATURE)
        return NULL;
    struct uprobe *uprobe_nit = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_sdt = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_eit = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_tdt = va_arg(args, struct uprobe *);

    struct upipe_ts_sig *upipe_ts_sig = malloc(sizeof(struct upipe_ts_sig));
    if (unlikely(upipe_ts_sig == NULL)) {
        uprobe_release(uprobe_nit);
        uprobe_release(uprobe_sdt);
        uprobe_release(uprobe_eit);
        uprobe_release(uprobe_tdt);
        return NULL;
    }

    struct upipe *upipe = upipe_ts_sig_to_upipe(upipe_ts_sig);
    upipe_init(upipe, mgr, uprobe);

    upipe_ts_sig_init_urefcount(upipe);
    upipe_ts_sig_init_uref_mgr(upipe);
    upipe_ts_sig_init_ubuf_mgr(upipe);
    upipe_ts_sig_init_uclock(upipe);
    upipe_ts_sig_init_output(upipe);
    upipe_ts_sig_init_service_mgr(upipe);
    upipe_ts_sig_init_sub_services(upipe);
    upipe_ts_sig_init_output_mgr(upipe);
    upipe_ts_sig_store_flow_def(upipe, NULL);

    upipe_ts_sig_output_init(upipe_ts_sig_output_to_upipe(
                                upipe_ts_sig_to_nit_output(upipe_ts_sig)),
                              &upipe_ts_sig->output_mgr, uprobe_nit);
    upipe_ts_sig_output_init(upipe_ts_sig_output_to_upipe(
                                upipe_ts_sig_to_sdt_output(upipe_ts_sig)),
                              &upipe_ts_sig->output_mgr, uprobe_sdt);
    upipe_ts_sig_output_init(upipe_ts_sig_output_to_upipe(
                                upipe_ts_sig_to_eit_output(upipe_ts_sig)),
                              &upipe_ts_sig->output_mgr, uprobe_eit);
    upipe_ts_sig_output_init(upipe_ts_sig_output_to_upipe(
                                upipe_ts_sig_to_tdt_output(upipe_ts_sig)),
                              &upipe_ts_sig->output_mgr, uprobe_tdt);

    upipe_ts_sig->flow_def = NULL;
    upipe_ts_sig->frozen = false;

    upipe_ts_sig->nit_version = 0;
    ulist_init(&upipe_ts_sig->nit_sections);
    upipe_ts_sig->nit_nb_sections = 0;
    upipe_ts_sig->nit_size = 0;
    upipe_ts_sig->nit_interval = 0;
    upipe_ts_sig->nit_cr_sys = 0;
    upipe_ts_sig->nit_sent = false;

    upipe_ts_sig->sdt_version = 0;
    ulist_init(&upipe_ts_sig->sdt_sections);
    upipe_ts_sig->sdt_nb_sections = 0;
    upipe_ts_sig->sdt_size = 0;
    upipe_ts_sig->sdt_interval = 0;
    upipe_ts_sig->sdt_cr_sys = 0;
    upipe_ts_sig->sdt_sent = false;

    upipe_ts_sig->tdt_interval = 0;
    upipe_ts_sig->tdt_cr_sys = 0;

    upipe_throw_ready(upipe);
    upipe_ts_sig_demand_uref_mgr(upipe);
    if (likely(upipe_ts_sig->uref_mgr != NULL)) {
        struct uref *flow_format = uref_alloc_control(upipe_ts_sig->uref_mgr);
        uref_flow_set_def(flow_format, "block.mpegtspsi.");
        upipe_ts_sig_demand_ubuf_mgr(upipe, flow_format);
    }
    return upipe;
}

/** @internal @This sends a PSI section.
 *
 * @param upipe description structure of the pipe
 * @param output pointer to upipe_ts_sig_output pipe
 * @param sections ulist of PSI sections to send
 */
static void upipe_ts_sig_send(struct upipe *upipe, struct upipe *output_pipe,
                              struct uchain *sections)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_ts_sig_output *output =
        upipe_ts_sig_output_from_upipe(output_pipe);
    struct uchain *section_chain;
    ulist_foreach (sections, section_chain) {
        bool last = ulist_is_last(sections, section_chain);
        struct ubuf *ubuf = ubuf_dup(ubuf_from_uchain(section_chain));
        struct uref *uref = uref_alloc(sig->uref_mgr);
        size_t ubuf_size;
        if (unlikely(uref == NULL || ubuf == NULL ||
                     !ubase_check(ubuf_block_size(ubuf, &ubuf_size)))) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        uref_attach_ubuf(uref, ubuf);
        uref_block_set_start(uref);
        if (last)
            uref_block_set_end(uref);
        uref_clock_set_cr_sys(uref, output->cr_sys);
        upipe_ts_sig_output_output(output_pipe, uref, NULL);
        output->cr_sys +=
            (uint64_t)ubuf_size * UCLOCK_FREQ / output->octetrate +
            MIN_SECTION_INTERVAL;
    }
}

/** @internal @This builds a new output flow definition for NIT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_build_nit_flow_def(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_ts_sig_output *output = upipe_ts_sig_to_nit_output(sig);
    if (unlikely(!sig->nit_interval || !sig->nit_size)) {
        output->octetrate = 0;
        upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                           NULL);
        return;
    }

    /* duration during which we transmit the NIT */
    int64_t duration = sig->nit_interval -
        MIN_SECTION_INTERVAL * sig->nit_nb_sections;
    if (duration < MIN_SECTION_INTERVAL) {
        upipe_warn_va(upipe, "NIT interval is too short (missing %"PRId64")",
                      MIN_SECTION_INTERVAL - duration);
        duration = MIN_SECTION_INTERVAL;
    }

    output->octetrate = (uint64_t)sig->nit_size * UCLOCK_FREQ / duration;
    if (!output->octetrate)
        output->octetrate = 1;
    struct uref *flow_def = uref_alloc_control(sig->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtsnit.")) ||
        !ubase_check(uref_ts_flow_set_pid(flow_def, NIT_PID)) ||
        !ubase_check(uref_block_flow_set_size(flow_def, sig->nit_size)) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                sig->nit_interval / sig->nit_nb_sections)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def,
                output->octetrate)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                       flow_def);
    /* force sending flow definition immediately */
    upipe_ts_sig_output_output(upipe_ts_sig_output_to_upipe(output),
                               NULL, NULL);
}

/** @internal @This builds new NIT PSI sections.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_build_nit(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    if (unlikely(sig->flow_def == NULL || sig->ubuf_mgr == NULL))
        return;
    if (unlikely(sig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding a NIT");
        return;
    }

    uint64_t nid = DEFAULT_NID;
    uref_ts_flow_get_nid(sig->flow_def, &nid);
    const char *network_name_str = DEFAULT_NAME;
    uref_ts_flow_get_network_name(sig->flow_def, &network_name_str);
    size_t network_name_size;
    uint8_t *network_name =
        dvb_string_set((const uint8_t *)network_name_str,
            strlen(network_name_str), NATIVE_ENCODING,
            &network_name_size);

    size_t nit_descriptors_size = DESC40_HEADER_SIZE + network_name_size +
        uref_ts_flow_size_nit_descriptors(sig->flow_def);
    uint8_t nit_descriptors_buffer[nit_descriptors_size];
    uint8_t *nit_descriptors = nit_descriptors_buffer;
    desc40_init(nit_descriptors);
    desc_set_length(nit_descriptors, network_name_size);
    desc40_set_networkname(nit_descriptors, network_name, network_name_size);
    free(network_name);
    uref_ts_flow_extract_nit_descriptors(sig->flow_def,
            nit_descriptors + DESC40_HEADER_SIZE + network_name_size);

    upipe_notice_va(upipe, "new NIT nid=%"PRIu64" name=\"%s\" version=%"PRIu8,
                    nid, network_name_str, sig->nit_version);

    unsigned int nb_sections = 0;
    uint64_t i = 0;
    uint64_t ts_number = 0;
    uref_ts_flow_get_nit_ts(sig->flow_def, &ts_number);
    uint64_t total_size = 0;

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&sig->nit_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));

    do {
        if (unlikely(nb_sections >= PSI_TABLE_MAX_SECTIONS)) {
            upipe_warn(upipe, "NIT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        struct ubuf *ubuf = ubuf_block_alloc(sig->ubuf_mgr,
                                             PSI_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        nit_init(buffer, true);
        /* set length later */
        psi_set_length(buffer, PSI_MAX_SIZE);
        nit_set_nid(buffer, nid);
        psi_set_version(buffer, sig->nit_version);
        psi_set_current(buffer);
        psi_set_section(buffer, nb_sections);
        /* set last section in the end */
        nit_set_desclength(buffer, PSI_MAX_SIZE + PSI_HEADER_SIZE -
                                   NIT_HEADER_SIZE - NIT_HEADER2_SIZE);

        uint8_t k = 0;
        uint8_t *descs = nit_get_descs(buffer);
        while (nit_descriptors_size > 0) {
            uint8_t *desc = descs_get_desc(descs, k);
            uint8_t desc_length = DESC_HEADER_SIZE +
                                  desc_get_length(nit_descriptors);
            if (desc == NULL || !descs_validate_desc(descs, desc, desc_length))
                break;
            memcpy(desc, nit_descriptors, desc_length);
            nit_descriptors += desc_length;
            nit_descriptors_size -= desc_length;
            k++;
        }
        nit_set_desclength(buffer,
                descs_get_desc(descs, k) - buffer - NIT_HEADER_SIZE);

        uint8_t *nith = nit_get_header2(buffer);
        nith_init(nith);
        /* set length later */
        nith_set_tslength(nith, PSI_MAX_SIZE -
                (nith + NIT_HEADER2_SIZE - (buffer + PSI_HEADER_SIZE)));

        uint16_t j = 0;
        uint8_t *ts;
        while ((ts = nit_get_ts(buffer, j)) != NULL && i < ts_number &&
               !nit_descriptors_size) {
            uint64_t tsid, onid;
            if (!ubase_check(uref_ts_flow_get_nit_ts_tsid(sig->flow_def,
                            &tsid, i)) ||
                !ubase_check(uref_ts_flow_get_nit_ts_onid(sig->flow_def,
                            &onid, i))) {
                i++;
                continue;
            }
            size_t descriptors_size =
                uref_ts_flow_size_nit_ts_descriptors(sig->flow_def, i);
            if (!nit_validate_ts(buffer, ts, descriptors_size)) {
                if (j)
                    break;
                upipe_err_va(upipe, "NIT ts too large");
                ubuf_free(ubuf);
                upipe_throw_error(upipe, UBASE_ERR_INVALID);
                return;
            }

            upipe_notice_va(upipe, " * ts tsid=%"PRIu64" onid=%"PRIu64,
                            tsid, onid);

            j++;
            nitn_init(ts);
            nitn_set_tsid(ts, tsid);
            nitn_set_onid(ts, onid);
            nitn_set_desclength(ts, descriptors_size);
            if (descriptors_size)
                uref_ts_flow_extract_nit_ts_descriptors(sig->flow_def,
                        descs_get_desc(nitn_get_descs(ts), 0), i);
            i++;
        }

        nith_set_tslength(nith, ts - nith - NIT_HEADER2_SIZE);
        nit_set_length(buffer, ts - buffer - NIT_HEADER_SIZE);
        uint16_t nit_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);

        ubuf_block_resize(ubuf, 0, nit_size);
        ulist_add(&sig->nit_sections, ubuf_to_uchain(ubuf));
        nb_sections++;
        total_size += nit_size;
    } while (i < ts_number);

    ulist_foreach (&sig->nit_sections, section_chain) {
        struct ubuf *ubuf = ubuf_from_uchain(section_chain);
        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        psi_set_lastsection(buffer, nb_sections - 1);
        psi_set_crc(buffer);

        ubuf_block_unmap(ubuf, 0);
    }

    upipe_notice_va(upipe, "end NIT (%u sections)", nb_sections);

    sig->nit_nb_sections = nb_sections;
    sig->nit_size = total_size;
    sig->nit_sent = false;
}

/** @internal @This sends a NIT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys cr_sys of the next muxed packet
 */
static void upipe_ts_sig_send_nit(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    if (unlikely(sig->flow_def == NULL || ulist_empty(&sig->nit_sections) ||
                 !sig->nit_interval ||
                 sig->nit_cr_sys + sig->nit_interval > cr_sys))
        return;

    struct upipe_ts_sig_output *output = upipe_ts_sig_to_nit_output(sig);
    if (cr_sys < output->cr_sys)
        return;
    output->cr_sys = cr_sys;
    sig->nit_cr_sys = cr_sys;
    if (sig->nit_sent) {
        sig->nit_version++;
        sig->nit_version &= 0x1f;
        sig->nit_sent = false;
    }

    upipe_verbose_va(upipe, "sending NIT (%"PRIu64")", cr_sys);
    upipe_ts_sig_send(upipe, upipe_ts_sig_output_to_upipe(output),
                      &sig->nit_sections);
}

/** @internal @This builds a new output flow definition for SDT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_build_sdt_flow_def(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_ts_sig_output *output = upipe_ts_sig_to_sdt_output(sig);
    if (unlikely(!sig->sdt_interval || !sig->sdt_size)) {
        output->octetrate = 0;
        upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                           NULL);
        return;
    }

    /* duration during which we transmit the SDT */
    int64_t duration = sig->sdt_interval -
        MIN_SECTION_INTERVAL * sig->sdt_nb_sections;
    if (duration < MIN_SECTION_INTERVAL) {
        upipe_warn_va(upipe, "SDT interval is too short (missing %"PRId64")",
                      MIN_SECTION_INTERVAL - duration);
        duration = MIN_SECTION_INTERVAL;
    }

    output->octetrate = (uint64_t)sig->sdt_size * UCLOCK_FREQ / duration;
    if (!output->octetrate)
        output->octetrate = 1;
    struct uref *flow_def = uref_alloc_control(sig->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtssdt.")) ||
        !ubase_check(uref_ts_flow_set_pid(flow_def, SDT_PID)) ||
        !ubase_check(uref_block_flow_set_size(flow_def, sig->sdt_size)) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                sig->sdt_interval / sig->sdt_nb_sections)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def,
                output->octetrate)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                       flow_def);
    /* force sending flow definition immediately */
    upipe_ts_sig_output_output(upipe_ts_sig_output_to_upipe(output),
                               NULL, NULL);
}

/** @internal @This builds new SDT PSI sections.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_build_sdt(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    if (unlikely(sig->flow_def == NULL || sig->ubuf_mgr == NULL))
        return;
    if (unlikely(sig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding an SDT");
        return;
    }

    uint64_t tsid = DEFAULT_TSID;
    uref_flow_get_id(sig->flow_def, &tsid);
    uint64_t onid = DEFAULT_NID;
    uref_ts_flow_get_onid(sig->flow_def, &onid);

    upipe_notice_va(upipe,
                    "new SDT tsid=%"PRIu64" onid=%"PRIu64" version=%"PRIu8,
                    tsid, onid, sig->sdt_version);

    unsigned int nb_sections = 0;
    struct uchain *service_chain = &sig->services;
    uint64_t total_size = 0;

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&sig->sdt_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));

    do {
        if (unlikely(nb_sections >= PSI_TABLE_MAX_SECTIONS)) {
            upipe_warn(upipe, "SDT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        struct ubuf *ubuf = ubuf_block_alloc(sig->ubuf_mgr,
                                             PSI_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        sdt_init(buffer, true);
        /* set length later */
        psi_set_length(buffer, PSI_MAX_SIZE);
        sdt_set_tsid(buffer, tsid);
        sdt_set_onid(buffer, onid);
        psi_set_version(buffer, sig->sdt_version);
        psi_set_current(buffer);
        psi_set_section(buffer, nb_sections);
        /* set last section in the end */

        uint16_t j = 0;
        uint8_t *service;
        while ((service = sdt_get_service(buffer, j)) != NULL &&
               !ulist_is_last(&sig->services, service_chain)) {
            service_chain = service_chain->next;

            struct upipe_ts_sig_service *upipe_ts_sig_service =
                upipe_ts_sig_service_from_uchain(service_chain);
            uint64_t sid;
            if (upipe_ts_sig_service->flow_def == NULL ||
                !ubase_check(uref_flow_get_id(upipe_ts_sig_service->flow_def,
                            &sid)))
                continue;
            uint8_t service_type = 1;
            bool eit, eitschedule, ca;
            uint8_t running = 5; /* running */
            const char *service_name_str = DEFAULT_NAME;
            const char *provider_name_str = DEFAULT_NAME;
            uref_ts_flow_get_service_type(upipe_ts_sig_service->flow_def,
                                          &service_type);
            eit = ubase_check(uref_ts_flow_get_eit(upipe_ts_sig_service->flow_def));
            eitschedule = ubase_check(uref_ts_flow_get_eit_schedule(upipe_ts_sig_service->flow_def));
            ca = ubase_check(uref_ts_flow_get_scrambled(upipe_ts_sig_service->flow_def));
            uref_ts_flow_get_running_status(upipe_ts_sig_service->flow_def,
                                            &running);
            uref_flow_get_name(upipe_ts_sig_service->flow_def,
                               &service_name_str);
            uref_ts_flow_get_provider_name(upipe_ts_sig_service->flow_def,
                                           &provider_name_str);
            size_t service_name_size, provider_name_size;
            uint8_t *service_name =
                dvb_string_set((const uint8_t *)service_name_str,
                    strlen(service_name_str), NATIVE_ENCODING,
                    &service_name_size);
            uint8_t *provider_name =
                dvb_string_set((const uint8_t *)provider_name_str,
                    strlen(provider_name_str), NATIVE_ENCODING,
                    &provider_name_size);


            size_t descriptors_size =
                uref_ts_flow_size_sdt_descriptors(upipe_ts_sig_service->flow_def);
            if (!sdt_validate_service(buffer, service,
                        descriptors_size + DESC48_HEADER_SIZE +
                        service_name_size + 1 + provider_name_size + 1)) {
                if (j)
                    break;
                upipe_err_va(upipe, "SDT service too large");
                free(service_name);
                free(provider_name);
                ubuf_free(ubuf);
                upipe_throw_error(upipe, UBASE_ERR_INVALID);
                return;
            }

            upipe_notice_va(upipe,
                    " * service sid=%"PRIu64" name=\"%s\" provider=\"%s\"",
                    sid, service_name_str, provider_name_str);

            j++;
            sdtn_init(service);
            sdtn_set_sid(service, sid);
            if (eit)
                sdtn_set_eitpresent(service);
            if (eitschedule)
                sdtn_set_eitschedule(service);
            sdtn_set_running(service, running);
            if (ca)
                sdtn_set_ca(service);
            sdtn_set_desclength(service,
                        descriptors_size + DESC48_HEADER_SIZE +
                        service_name_size + 1 + provider_name_size + 1);
            uint8_t *desc = descs_get_desc(sdtn_get_descs(service), 0);
            desc48_init(desc);
            desc48_set_type(desc, service_type);
            desc48_set_provider(desc, provider_name, provider_name_size);
            desc48_set_service(desc, service_name, service_name_size);
            desc48_set_length(desc);
            free(provider_name);
            free(service_name);

            if (descriptors_size)
                uref_ts_flow_extract_sdt_descriptors(upipe_ts_sig_service->flow_def,
                        descs_get_desc(sdtn_get_descs(service), 1));
        }

        sdt_set_length(buffer, service - buffer - SDT_HEADER_SIZE);
        uint16_t sdt_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);

        ubuf_block_resize(ubuf, 0, sdt_size);
        ulist_add(&sig->sdt_sections, ubuf_to_uchain(ubuf));
        nb_sections++;
        total_size += sdt_size;
    } while (!ulist_is_last(&sig->services, service_chain));

    ulist_foreach (&sig->sdt_sections, section_chain) {
        struct ubuf *ubuf = ubuf_from_uchain(section_chain);
        uint8_t *buffer;
        int size = -1;
        if (!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer))) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        psi_set_lastsection(buffer, nb_sections - 1);
        psi_set_crc(buffer);

        ubuf_block_unmap(ubuf, 0);
    }

    upipe_notice_va(upipe, "end SDT (%u sections)", nb_sections);

    sig->sdt_nb_sections = nb_sections;
    sig->sdt_size = total_size;
    sig->sdt_sent = false;
}

/** @internal @This sends a SDT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys cr_sys of the next muxed packet
 */
static void upipe_ts_sig_send_sdt(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    if (unlikely(sig->flow_def == NULL || ulist_empty(&sig->sdt_sections) ||
                 !sig->sdt_interval ||
                 sig->sdt_cr_sys + sig->sdt_interval > cr_sys))
        return;

    struct upipe_ts_sig_output *output = upipe_ts_sig_to_sdt_output(sig);
    if (cr_sys < output->cr_sys)
        return;
    output->cr_sys = cr_sys;
    sig->sdt_cr_sys = cr_sys;
    if (sig->sdt_sent) {
        sig->sdt_version++;
        sig->sdt_version &= 0x1f;
        sig->sdt_sent = false;
    }

    upipe_verbose_va(upipe, "sending SDT (%"PRIu64")", cr_sys);
    upipe_ts_sig_send(upipe, upipe_ts_sig_output_to_upipe(output),
                      &sig->sdt_sections);
}

/** @internal @This builds a new output flow definition for EIT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_build_eit_flow_def(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_ts_sig_output *output = upipe_ts_sig_to_eit_output(sig);

    uint64_t size = 0;
    uint64_t octetrate = 0;
    struct urational section_freq;
    section_freq.num = 0;
    section_freq.den = 1;
    struct uchain *uchain;
    ulist_foreach (&sig->services, uchain) {
        struct upipe_ts_sig_service *service =
            upipe_ts_sig_service_from_uchain(uchain);
        if (!service->eit_interval)
            continue;
        size += service->eit_size;

        if (service->eit_nb_sections) {
            struct urational freq;
            freq.num = 1;
            freq.den = service->eit_interval / service->eit_nb_sections;
            urational_simplify(&freq);
            section_freq = urational_add(&section_freq, &freq);
        }

        /* duration during which we transmit the EIT */
        int64_t duration = service->eit_interval -
            MIN_SECTION_INTERVAL * service->eit_nb_sections;
        if (duration < MIN_SECTION_INTERVAL) {
            upipe_warn_va(upipe_ts_sig_service_to_upipe(service),
                          "EIT interval is too short (missing %"PRId64")",
                          MIN_SECTION_INTERVAL - duration);
            duration = MIN_SECTION_INTERVAL;
        }
        octetrate += (uint64_t)service->eit_size * UCLOCK_FREQ / duration;
    }

    if (unlikely(!size)) {
        output->octetrate = 0;
        upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                           NULL);
        return;
    }

    output->octetrate = octetrate;
    if (!output->octetrate)
        output->octetrate = 1;

    struct uref *flow_def = uref_alloc_control(sig->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtseit.")) ||
        !ubase_check(uref_ts_flow_set_pid(flow_def, EIT_PID)) ||
        !ubase_check(uref_block_flow_set_size(flow_def, size)) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                section_freq.den / section_freq.num)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def, octetrate)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                       flow_def);
    /* force sending flow definition immediately */
    upipe_ts_sig_output_output(upipe_ts_sig_output_to_upipe(output),
                               NULL, NULL);
}

/** @internal @This sends a EIT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys cr_sys of the next muxed packet
 */
static void upipe_ts_sig_send_eit(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    if (unlikely(sig->flow_def == NULL))
        return;

    struct upipe_ts_sig_output *output = upipe_ts_sig_to_eit_output(sig);
    if (cr_sys < output->cr_sys)
        return;

    struct uchain *uchain;
    ulist_foreach (&sig->services, uchain) {
        struct upipe_ts_sig_service *service =
            upipe_ts_sig_service_from_uchain(uchain);
        if (ulist_empty(&service->eit_sections) || !service->eit_interval ||
            service->eit_cr_sys + service->eit_interval > cr_sys)
            continue;

        output->cr_sys = cr_sys;
        service->eit_cr_sys = cr_sys;
        if (service->eit_sent) {
            service->eit_version++;
            service->eit_version &= 0x1f;
            service->eit_sent = false;
        }

        upipe_verbose_va(upipe_ts_sig_service_to_upipe(service),
                         "sending EIT (%"PRIu64")", cr_sys);
        upipe_ts_sig_send(upipe, upipe_ts_sig_output_to_upipe(output),
                          &service->eit_sections);
        return;
    }
}

/** @internal @This builds a new output flow definition for TDT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_build_tdt_flow_def(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    struct upipe_ts_sig_output *output = upipe_ts_sig_to_tdt_output(sig);
    if (unlikely(!sig->tdt_interval)) {
        output->octetrate = 0;
        upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                           NULL);
        return;
    }

    /* duration during which we transmit the TDT */
    int64_t duration = sig->tdt_interval - MIN_SECTION_INTERVAL;
    if (duration < MIN_SECTION_INTERVAL) {
        upipe_warn_va(upipe, "TDT interval is too short (missing %"PRId64")",
                      MIN_SECTION_INTERVAL - duration);
        duration = MIN_SECTION_INTERVAL;
    }

    output->octetrate = (uint64_t)TDT_HEADER_SIZE * UCLOCK_FREQ / duration;
    if (!output->octetrate)
        output->octetrate = 1;
    struct uref *flow_def = uref_alloc_control(sig->uref_mgr);
    if (unlikely(flow_def == NULL ||
        !ubase_check(uref_flow_set_def(flow_def,
                                       "block.mpegtspsi.mpegtstdt.")) ||
        !ubase_check(uref_ts_flow_set_pid(flow_def, TDT_PID)) ||
        !ubase_check(uref_block_flow_set_size(flow_def, TDT_HEADER_SIZE)) ||
        !ubase_check(uref_ts_flow_set_psi_section_interval(flow_def,
                sig->sdt_interval)) ||
        !ubase_check(uref_block_flow_set_octetrate(flow_def,
                output->octetrate)) ||
        !ubase_check(uref_ts_flow_set_tb_rate(flow_def, TB_RATE_PSI)))) {
        uref_free(flow_def);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_sig_output_store_flow_def(upipe_ts_sig_output_to_upipe(output),
                                       flow_def);
    /* force sending flow definition immediately */
    upipe_ts_sig_output_output(upipe_ts_sig_output_to_upipe(output),
                               NULL, NULL);
}

/** @internal @This sends a TDT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys cr_sys of the next muxed packet
 * @param latency latency before the packet is output
 */
static void upipe_ts_sig_send_tdt(struct upipe *upipe, uint64_t cr_sys,
                                  uint64_t latency)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    time_t now;
    if (unlikely(sig->flow_def == NULL || sig->uclock == NULL ||
                 !sig->tdt_interval ||
                 sig->tdt_cr_sys + sig->tdt_interval > cr_sys ||
                 (now = uclock_mktime(sig->uclock, cr_sys + latency)) ==
                 (time_t)-1))
        return;

    struct upipe_ts_sig_output *output = upipe_ts_sig_to_tdt_output(sig);
    if (cr_sys < output->cr_sys)
        return;
    sig->tdt_cr_sys = cr_sys;
    output->cr_sys = cr_sys;

    upipe_verbose_va(upipe, "sending TDT (%"PRIu64")", cr_sys);

    struct uref *uref = uref_block_alloc(sig->uref_mgr, sig->ubuf_mgr,
                                         TDT_HEADER_SIZE);
    if (unlikely(uref == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int size = -1;
    if (!ubase_check(uref_block_write(uref, 0, &size, &buffer))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    tdt_init(buffer);
    tdt_set_utc(buffer, dvb_time_encode_UTC(now));
    uref_block_unmap(uref, 0);

    uref_block_set_start(uref);
    uref_block_set_end(uref);
    uref_clock_set_cr_sys(uref, output->cr_sys);
    upipe_ts_sig_output_output(upipe_ts_sig_output_to_upipe(output),
                               uref, NULL);
    output->cr_sys +=
        (uint64_t)TDT_HEADER_SIZE * UCLOCK_FREQ / output->octetrate +
        MIN_SECTION_INTERVAL;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_sig_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    uint64_t tsid, nid, onid;
    UBASE_RETURN(uref_flow_get_id(flow_def, &tsid))
    UBASE_RETURN(uref_ts_flow_get_nid(flow_def, &nid))
    UBASE_RETURN(uref_ts_flow_get_onid(flow_def, &onid))

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    bool nit_change = sig->flow_def == NULL ||
        uref_ts_flow_cmp_nid(flow_def, sig->flow_def) ||
        uref_ts_flow_cmp_network_name(flow_def, sig->flow_def) ||
        uref_ts_flow_compare_nit_descriptors(flow_def, sig->flow_def) ||
        uref_ts_flow_cmp_nit_ts(flow_def, sig->flow_def);
    if (!nit_change) {
        uint64_t ts_number = 0;
        uref_ts_flow_get_nit_ts(flow_def, &ts_number);
        for (uint64_t i = 0; !nit_change && i < ts_number; i++) {
            nit_change =
                uref_ts_flow_cmp_nit_ts_tsid(flow_def, sig->flow_def, i) ||
                uref_ts_flow_cmp_nit_ts_onid(flow_def, sig->flow_def, i) ||
                uref_ts_flow_compare_nit_ts_descriptors(flow_def,
                        sig->flow_def, i);
        }
    }

    bool sdt_change = sig->flow_def == NULL ||
        uref_flow_cmp_id(flow_def, sig->flow_def) ||
        uref_ts_flow_cmp_onid(flow_def, sig->flow_def);

    bool eit_change = sdt_change;

    uref_free(sig->flow_def);
    sig->flow_def = flow_def_dup;

    if (nit_change) {
        upipe_ts_sig_build_nit(upipe);
        upipe_ts_sig_build_nit_flow_def(upipe);
    }

    if (sdt_change) {
        upipe_ts_sig_build_sdt(upipe);
        upipe_ts_sig_build_sdt_flow_def(upipe);
    }

    if (eit_change) {
        struct uchain *uchain;
        ulist_foreach (&sig->services, uchain) {
            struct upipe_ts_sig_service *service =
                upipe_ts_sig_service_from_uchain(uchain);
            upipe_ts_sig_service_build_eit(upipe_ts_sig_service_to_upipe(service));
        }
        upipe_ts_sig_build_eit_flow_def(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @This prepares the next PSI sections for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @param latency latency before the packet is output
 * @return an error code
 */
static int _upipe_ts_sig_prepare(struct upipe *upipe, uint64_t cr_sys,
                                 uint64_t latency)
{
    upipe_ts_sig_send_nit(upipe, cr_sys);
    upipe_ts_sig_send_sdt(upipe, cr_sys);
    upipe_ts_sig_send_eit(upipe, cr_sys);
    upipe_ts_sig_send_tdt(upipe, cr_sys, latency);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_sig_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);

    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_sig_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_sig_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_sig_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_sig_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sig_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_sig_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_sig_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_sig_iterate_sub(upipe, p);
        }

        case UPIPE_TS_MUX_GET_NIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            *interval_p = sig->nit_interval;
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_SET_NIT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            sig->nit_interval = va_arg(args, uint64_t);
            upipe_ts_sig_build_nit_flow_def(upipe);
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_GET_SDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            *interval_p = sig->sdt_interval;
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_SET_SDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            sig->sdt_interval = va_arg(args, uint64_t);
            upipe_ts_sig_build_sdt_flow_def(upipe);
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_GET_TDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            *interval_p = sig->tdt_interval;
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_SET_TDT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            sig->tdt_interval = va_arg(args, uint64_t);
            if (sig->tdt_interval)
                upipe_ts_sig_require_uclock(upipe);
            upipe_ts_sig_build_tdt_flow_def(upipe);
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_FREEZE_PSI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            sig->frozen = true;
            return UBASE_ERR_NONE;
        }

        case UPIPE_TS_SIG_PREPARE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SIG_SIGNATURE)
            uint64_t cr_sys = va_arg(args, uint64_t);
            uint64_t latency = va_arg(args, uint64_t);
            return _upipe_ts_sig_prepare(upipe, cr_sys, latency);
        }
        case UPIPE_TS_SIG_GET_NIT_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SIG_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_ts_sig_output_to_upipe(
                    upipe_ts_sig_to_nit_output(
                        upipe_ts_sig_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_SIG_GET_SDT_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SIG_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_ts_sig_output_to_upipe(
                    upipe_ts_sig_to_sdt_output(
                        upipe_ts_sig_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_SIG_GET_EIT_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SIG_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_ts_sig_output_to_upipe(
                    upipe_ts_sig_to_eit_output(
                        upipe_ts_sig_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_SIG_GET_TDT_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_SIG_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_ts_sig_output_to_upipe(
                    upipe_ts_sig_to_tdt_output(
                        upipe_ts_sig_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_sig_free(struct upipe *upipe)
{
    struct upipe_ts_sig *sig = upipe_ts_sig_from_upipe(upipe);
    upipe_ts_sig_output_clean(upipe_ts_sig_output_to_upipe(
                upipe_ts_sig_to_nit_output(sig)));
    upipe_ts_sig_output_clean(upipe_ts_sig_output_to_upipe(
                upipe_ts_sig_to_sdt_output(sig)));
    upipe_ts_sig_output_clean(upipe_ts_sig_output_to_upipe(
                upipe_ts_sig_to_eit_output(sig)));
    upipe_ts_sig_output_clean(upipe_ts_sig_output_to_upipe(
                upipe_ts_sig_to_tdt_output(sig)));

    upipe_throw_dead(upipe);

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&sig->nit_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));
    while ((section_chain = ulist_pop(&sig->sdt_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));
    uref_free(sig->flow_def);

    upipe_ts_sig_clean_sub_services(upipe);
    upipe_ts_sig_clean_ubuf_mgr(upipe);
    upipe_ts_sig_clean_uref_mgr(upipe);
    upipe_ts_sig_clean_uclock(upipe);
    upipe_ts_sig_clean_output(upipe);
    upipe_ts_sig_clean_urefcount(upipe);

    upipe_clean(upipe);
    free(sig);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_sig_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_SIG_SIGNATURE,

    .upipe_alloc = _upipe_ts_sig_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_ts_sig_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_sig pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_sig_mgr_alloc(void)
{
    return &upipe_ts_sig_mgr;
}

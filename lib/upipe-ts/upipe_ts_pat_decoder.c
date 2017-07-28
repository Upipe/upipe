/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the program association table of TS streams
 * Normative references:
 *  - ISO/IEC 13818-1:2007(E) (MPEG-2 Systems)
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtspat."

/** @hidden */
static int upipe_ts_patd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_patd pipe. */
struct upipe_ts_patd {
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

    /** currently in effect PAT table */
    UPIPE_TS_PSID_TABLE_DECLARE(pat);
    /** PAT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_pat);
    /** current TSID */
    int tsid;
    /** NIT flow definition */
    struct uref *nit;
    /** list of programs */
    struct uchain programs;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_patd, upipe, UPIPE_TS_PATD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_patd, urefcount, upipe_ts_patd_free)
UPIPE_HELPER_VOID(upipe_ts_patd)
UPIPE_HELPER_OUTPUT(upipe_ts_patd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_patd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_patd_check,
                      upipe_ts_patd_register_output_request,
                      upipe_ts_patd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_patd, flow_def_input, flow_def_attr)

/** @internal @This allocates a ts_patd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_patd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_patd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    upipe_ts_patd_init_urefcount(upipe);
    upipe_ts_patd_init_output(upipe);
    upipe_ts_patd_init_ubuf_mgr(upipe);
    upipe_ts_patd_init_flow_def(upipe);
    upipe_ts_psid_table_init(upipe_ts_patd->pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
    upipe_ts_patd->tsid = -1;
    upipe_ts_patd->nit = NULL;
    ulist_init(&upipe_ts_patd->programs);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This cleans up the list of programs.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_patd_clean_programs(struct upipe *upipe)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_patd->programs, uchain, uchain_tmp) {
        struct uref *flow_def = uref_from_uchain(uchain);
        ulist_delete(uchain);
        uref_free(flow_def);
    }
}

/** @internal @This checks if the program is already in the table with different
 * parameters.
 *
 * @param upipe description structure of the pipe
 * @param wanted_program program to check
 * @return true if there is no different instance of the service in the table
 */
static bool upipe_ts_patd_table_compare_program(struct upipe *upipe,
                                                const uint8_t *wanted_program)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_patd->next_pat, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        const uint8_t *program;
        int j = 0;
        while ((program = pat_get_program((uint8_t *)section, j)) != NULL) {
            j++;
            if (patn_get_program(program) == patn_get_program(wanted_program)) {
                bool result = (program == wanted_program) ||
                     !memcmp(program, wanted_program, PAT_PROGRAM_SIZE);
                uref_block_unmap(section_uref, 0);
                return result;
            }
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This validates the next PAT.
 *
 * @param upipe description structure of the pipe
 * @return false if the PAT is invalid
 */
static bool upipe_ts_patd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_patd->next_pat, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (!pat_validate(section) || !psi_check_crc(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        const uint8_t *program;
        int j = 0;
        while ((program = pat_get_program((uint8_t *)section, j)) != NULL) {
            j++;
            /* check that the program number is not already in the table
             * with another PID */
            if (!upipe_ts_patd_table_compare_program(upipe, program)) {
                uref_block_unmap(section_uref, 0);
                return false;
            }
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This gets the cr_sys of the earliest section of the next PAT,
 * and sends the new rap event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure triggering the event
 */
static void upipe_ts_patd_table_rap(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    uint64_t cr_sys = UINT64_MAX;
    upipe_ts_psid_table_foreach (upipe_ts_patd->next_pat, section) {
        uint64_t section_cr_sys;
        if (ubase_check(uref_clock_get_cr_sys(section, &section_cr_sys)) &&
            section_cr_sys < cr_sys)
            cr_sys = section_cr_sys;
    }
    if (cr_sys != UINT64_MAX) {
        uref_clock_set_rap_sys(uref, cr_sys);
        upipe_throw_new_rap(upipe, uref);
    }
}

/** @internal @This builds the flow definition corresponding to a program.
 *
 * @param upipe description structure of the pipe
 * @param program program structure in PAT
 */
static void upipe_ts_patd_build_program(struct upipe *upipe,
                                        const uint8_t *program)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    struct uref *flow_def = uref_dup(upipe_ts_patd->flow_def_input);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint16_t program_number = patn_get_program(program);
    uint16_t pid = patn_get_pid(program);

    if (program_number) {
        /* duplicates are allowed in PAT */
        struct uchain *uchain;
        ulist_foreach (&upipe_ts_patd->programs, uchain) {
            struct uref *uref = uref_from_uchain(uchain);
            uint64_t flow_id;
            if (ubase_check(uref_flow_get_id(uref, &flow_id)) &&
                flow_id == program_number) {
                uref_free(flow_def);
                return;
            }
        }

        /* prepare filter on table 2, current, program number */
        uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
        uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
        memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
        memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
        psi_set_syntax(filter);
        psi_set_syntax(mask);
        psi_set_current(filter);
        psi_set_current(mask);
        psi_set_tableid(filter, PMT_TABLE_ID);
        psi_set_tableid(mask, 0xff);
        psi_set_tableidext(filter, program_number);
        psi_set_tableidext(mask, 0xffff);

        UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
        UBASE_FATAL(upipe, uref_flow_set_id(flow_def, program_number))
        UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                    "block.mpegtspsi.mpegtspmt."))
        UBASE_FATAL(upipe, uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                    PSI_HEADER_SIZE_SYNTAX1))
        UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def, pid))
        ulist_add(&upipe_ts_patd->programs, uref_to_uchain(flow_def));

    } else if (pid == NIT_PID) {
        if (upipe_ts_patd->nit != NULL) {
            uref_free(flow_def);
            return;
        }

        /* NIT in DVB systems - prepare filter on table 0x40, current */
        uint8_t filter[PSI_HEADER_SIZE_SYNTAX1];
        uint8_t mask[PSI_HEADER_SIZE_SYNTAX1];
        memset(filter, 0, PSI_HEADER_SIZE_SYNTAX1);
        memset(mask, 0, PSI_HEADER_SIZE_SYNTAX1);
        psi_set_syntax(filter);
        psi_set_syntax(mask);
        psi_set_current(filter);
        psi_set_current(mask);
        psi_set_tableid(filter, NIT_TABLE_ID_ACTUAL);
        psi_set_tableid(mask, 0xff);

        UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
        UBASE_FATAL(upipe, uref_flow_set_raw_def(flow_def,
                                          "block.mpegtspsi.mpegtsdvbnit."))
        UBASE_FATAL(upipe, uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                                                PSI_HEADER_SIZE_SYNTAX1))
        UBASE_FATAL(upipe, uref_ts_flow_set_pid(flow_def, pid))
        upipe_ts_patd->nit = flow_def;

    } else
        uref_free(flow_def);
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_patd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    assert(upipe_ts_patd->flow_def_input != NULL);
    assert(upipe_ts_patd->ubuf_mgr != NULL);

    if (!upipe_ts_psid_table_section(upipe_ts_patd->next_pat, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_patd->pat) &&
        upipe_ts_psid_table_compare(upipe_ts_patd->pat,
                                    upipe_ts_patd->next_pat)) {
        /* Identical PAT. */
        upipe_ts_patd_table_rap(upipe, uref);
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_patd->next_pat,
                                               upipe_ts_patd->ubuf_mgr)) ||
        !upipe_ts_patd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid PAT section received");
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
        return;
    }

    uint16_t tsid = upipe_ts_psid_table_get_tableidext(upipe_ts_patd->next_pat);

    if (unlikely(tsid != upipe_ts_patd->tsid)) {
        struct uref *flow_def = upipe_ts_patd_alloc_flow_def_attr(upipe);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_ts_patd->tsid = tsid;
        UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
        UBASE_FATAL(upipe, uref_flow_set_id(flow_def, tsid))
        flow_def = upipe_ts_patd_store_flow_def_attr(upipe, flow_def);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        upipe_ts_patd_store_flow_def(upipe, flow_def);
        /* Force sending flow def */
        upipe_ts_patd_output(upipe, NULL, upump_p);
    }

    upipe_ts_patd_table_rap(upipe, uref);
    upipe_ts_patd_clean_programs(upipe);
    uref_free(upipe_ts_patd->nit);
    upipe_ts_patd->nit = NULL;

    upipe_ts_psid_table_foreach (upipe_ts_patd->next_pat, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        const uint8_t *program;
        int j = 0;
        while ((program = pat_get_program((uint8_t *)section, j)) != NULL) {
            j++;
            upipe_ts_patd_build_program(upipe, program);
        }

        uref_block_unmap(section_uref, 0);
    }

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_patd->pat))
        upipe_ts_psid_table_clean(upipe_ts_patd->pat);
    upipe_ts_psid_table_copy(upipe_ts_patd->pat, upipe_ts_patd->next_pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);

    upipe_split_throw_update(upipe);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_patd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_patd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_patd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_patd_output(upipe, NULL, NULL);
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
static int upipe_ts_patd_set_flow_def(struct upipe *upipe,
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
    upipe_ts_patd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This iterates over program flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return an error code
 */
static int upipe_ts_patd_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    assert(p != NULL);
    struct uchain *uchain;
    if (*p != NULL)
        uchain = uref_to_uchain(*p);
    else
        uchain = &upipe_ts_patd->programs;
    if (ulist_is_last(&upipe_ts_patd->programs, uchain)) {
        *p = NULL;
        return UBASE_ERR_NONE;
    }
    *p = uref_from_uchain(uchain->next);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the flow definition of the NIT.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the flow definition of the NIT
 * @return an error code
 */
static int _upipe_ts_patd_get_nit(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    if (upipe_ts_patd->nit != NULL) {
        *p = upipe_ts_patd->nit;
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_patd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_patd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_patd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_patd_iterate(upipe, p);
        }

        case UPIPE_TS_PATD_GET_NIT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PATD_SIGNATURE)
            struct uref **p = va_arg(args, struct uref **);
            return _upipe_ts_patd_get_nit(upipe, p);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_patd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    uref_free(upipe_ts_patd->nit);
    upipe_ts_psid_table_clean(upipe_ts_patd->pat);
    upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
    upipe_ts_patd_clean_programs(upipe);
    upipe_ts_patd_clean_output(upipe);
    upipe_ts_patd_clean_ubuf_mgr(upipe);
    upipe_ts_patd_clean_flow_def(upipe);
    upipe_ts_patd_clean_urefcount(upipe);
    upipe_ts_patd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_patd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PATD_SIGNATURE,

    .upipe_alloc = upipe_ts_patd_alloc,
    .upipe_input = upipe_ts_patd_input,
    .upipe_control = upipe_ts_patd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_patd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_patd_mgr_alloc(void)
{
    return &upipe_ts_patd_mgr;
}

/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the program association table of TS streams
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
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtspat."

/** @internal @This is the private context of a ts_patd pipe. */
struct upipe_ts_patd {
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** input flow definition */
    struct uref *flow_def_input;
    /** currently in effect PAT table */
    UPIPE_TS_PSID_TABLE_DECLARE(pat);
    /** PAT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_pat);
    /** current TSID */
    int tsid;
    /** list of programs */
    struct uchain programs;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_patd, upipe, UPIPE_TS_PATD_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_patd, EXPECTED_FLOW_DEF)
UPIPE_HELPER_OUTPUT(upipe_ts_patd, output, flow_def, flow_def_sent)

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
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_patd_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    upipe_ts_patd_init_output(upipe);
    upipe_ts_patd->flow_def_input = flow_def;
    upipe_ts_psid_table_init(upipe_ts_patd->pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
    upipe_ts_patd->tsid = -1;
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

/** @internal @This walks through the programs in a PAT. This is the first part:
 * read data from pat_program afterwards.
 *
 * @param upipe description structure of the pipe
 * @param sections PAT table
 * @param pat_program iterator pointing to program definition
 */
#define UPIPE_TS_PATD_TABLE_PEEK(upipe, sections, pat_program)              \
    upipe_ts_psid_table_foreach(sections, section) {                        \
        size_t size;                                                        \
        bool ret = uref_block_size(section, &size);                         \
        assert(ret);                                                        \
                                                                            \
        int offset = PAT_HEADER_SIZE;                                       \
        while (offset + PAT_PROGRAM_SIZE <= size - PSI_CRC_SIZE) {          \
            uint8_t program_buffer[PAT_PROGRAM_SIZE];                       \
            const uint8_t *pat_program = uref_block_peek(section, offset,   \
                                                         PAT_PROGRAM_SIZE,  \
                                                         program_buffer);   \
            if (unlikely(pat_program == NULL))                              \
                break;

/** @internal @This walks through the programs in a PAT. This is the second
 * part: do the actions afterwards.
 *
 * @param upipe description structure of the pipe
 * @param sections PAT table
 * @param pat_program iterator pointing to program definition
 */
#define UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, sections, pat_program)        \
            ret = uref_block_peek_unmap(section, offset, program_buffer,    \
                                        pat_program);                       \
            assert(ret);                                                    \
            offset += PAT_PROGRAM_SIZE;

/** @internal @This walks through the programs in a PAT. This is the last part.
 *
 * @param upipe description structure of the pipe
 * @param sections PAT table
 * @param pat_program iterator pointing to program definition
 */
#define UPIPE_TS_PATD_TABLE_PEEK_END(upipe, sections, pat_program)          \
        }                                                                   \
        if (unlikely(offset + PAT_PROGRAM_SIZE <= size - PSI_CRC_SIZE)) {   \
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);                     \
            break;                                                          \
        }                                                                   \
    }

/** @internal @This returns the PMT PID of the given program in the given
 * PAT.
 *
 * @param upipe description structure of the pipe
 * @param sections PAT table to check
 * @param wanted_program program number to check
 * @return PMT PID, or 0 if not found
 */
static uint16_t upipe_ts_patd_table_pmt_pid(struct upipe *upipe,
                                            struct uref **sections,
                                            uint16_t wanted_program)
{
    if (!upipe_ts_psid_table_validate(sections))
        return 0;

    UPIPE_TS_PATD_TABLE_PEEK(upipe, sections, pat_program)

    uint16_t program = patn_get_program(pat_program);
    uint16_t pid = patn_get_pid(pat_program);

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, sections, pat_program)

    if (program == wanted_program)
        return pid;

    UPIPE_TS_PATD_TABLE_PEEK_END(upipe, sections, pat_program)
    return 0;
}

/** @internal @This validates the next PAT.
 *
 * @param upipe description structure of the pipe
 * @return false if the PAT is invalid
 */
static bool upipe_ts_patd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    {
        upipe_ts_psid_table_foreach(upipe_ts_patd->next_pat, section) {
            if (!upipe_ts_psid_check_crc(section))
                return false;
        }
    }

    UPIPE_TS_PATD_TABLE_PEEK(upipe, upipe_ts_patd->next_pat, pat_program)

    uint16_t program = patn_get_program(pat_program);
    uint16_t pid = patn_get_pid(pat_program);

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, upipe_ts_patd->next_pat, pat_program)

    if (pid != upipe_ts_patd_table_pmt_pid(upipe, upipe_ts_patd->next_pat,
                                           program))
        return false;

    UPIPE_TS_PATD_TABLE_PEEK_END(upipe, upipe_ts_patd->next_pat, pat_program)
    return true;
}

/** @internal @This gets the systime of the earliest section of the next PAT,
 * and sends the new rap event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure triggering the event
 */
static void upipe_ts_patd_table_rap(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    uint64_t systime = UINT64_MAX;
    upipe_ts_psid_table_foreach(upipe_ts_patd->next_pat, section) {
        uint64_t section_systime;
        if (uref_clock_get_cr_sys(section, &section_systime) &&
            section_systime < systime)
            systime = section_systime;
    }
    if (systime != UINT64_MAX) {
        uref_clock_set_rap_sys(uref, systime);
        upipe_throw_new_rap(upipe, uref);
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_patd_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    bool ret;
    uint8_t buffer[PAT_HEADER_SIZE];
    const uint8_t *pat_header = uref_block_peek(uref, 0, PAT_HEADER_SIZE,
                                                buffer);
    if (unlikely(pat_header == NULL)) {
        upipe_warn(upipe, "invalid PAT section received");
        uref_free(uref);
        return;
    }
    bool validate = pat_validate(pat_header);
    uint16_t tsid = psi_get_tableidext(pat_header);
    ret = uref_block_peek_unmap(uref, 0, buffer, pat_header);
    assert(ret);

    if (unlikely(!validate)) {
        upipe_warn(upipe, "invalid PAT section received");
        uref_free(uref);
        return;
    }

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

    if (!upipe_ts_patd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid PAT section received");
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
        return;
    }

    if (unlikely(tsid != upipe_ts_patd->tsid)) {
        struct uref *flow_def = uref_dup(upipe_ts_patd->flow_def_input);
        if (unlikely(flow_def == NULL)) {
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            uref_free(uref);
            return;
        }
        upipe_ts_patd->tsid = tsid;
        if (unlikely(!uref_flow_set_def(flow_def, "void.") ||
                     !uref_flow_set_id(flow_def, tsid)))
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        upipe_ts_patd_store_flow_def(upipe, flow_def);
        /* Force sending flow def */
        upipe_throw_new_flow_def(upipe, flow_def);
    }

    upipe_ts_patd_table_rap(upipe, uref);
    upipe_ts_patd_clean_programs(upipe);

    UPIPE_TS_PATD_TABLE_PEEK(upipe, upipe_ts_patd->next_pat, pat_program)

    uint16_t program = patn_get_program(pat_program);
    uint16_t pid = patn_get_pid(pat_program);

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, upipe_ts_patd->next_pat, pat_program)

    if (program) {
        struct uref *flow_def = uref_dup(upipe_ts_patd->flow_def_input);
        if (likely(flow_def != NULL)) {
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
            psi_set_tableidext(filter, program);
            psi_set_tableidext(mask, 0xffff);

            if (unlikely(!uref_flow_set_def(flow_def, "void.") ||
                         !uref_flow_set_id(flow_def, program) ||
                         !uref_flow_set_raw_def(flow_def,
                             "block.mpegtspsi.mpegtspmt.") ||
                         !uref_ts_flow_set_psi_filter(flow_def, filter, mask,
                             PSI_HEADER_SIZE_SYNTAX1) ||
                         !uref_ts_flow_set_pid(flow_def, pid)))
                upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            ulist_add(&upipe_ts_patd->programs, uref_to_uchain(flow_def));
        } else
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
    }

    UPIPE_TS_PATD_TABLE_PEEK_END(upipe, upipe_ts_patd->next_pat, pat_program)

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_patd->pat))
        upipe_ts_psid_table_clean(upipe_ts_patd->pat);
    upipe_ts_psid_table_copy(upipe_ts_patd->pat, upipe_ts_patd->next_pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);

    upipe_split_throw_update(upipe);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_patd_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_ts_patd_work(upipe, uref);
}

/** @internal @This iterates over program flow definitions.
 *
 * @param upipe description structure of the pipe
 * @param p filled in with the next flow definition, initialize with NULL
 * @return false when no more flow definition is available
 */
static bool upipe_ts_patd_iterate(struct upipe *upipe, struct uref **p)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    assert(p != NULL);
    struct uchain *uchain;
    if (*p != NULL)
        uchain = uref_to_uchain(*p);
    else
        uchain = &upipe_ts_patd->programs;
    if (ulist_is_last(&upipe_ts_patd->programs, uchain))
        return false;
    *p = uref_from_uchain(uchain->next);
    return true;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_patd_control(struct upipe *upipe,
                                  enum upipe_command command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_patd_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_patd_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_patd_set_output(upipe, output);
        }
        case UPIPE_SPLIT_ITERATE: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_patd_iterate(upipe, p);
        }

        default:
            return false;
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
    uref_free(upipe_ts_patd->flow_def_input);
    upipe_ts_psid_table_clean(upipe_ts_patd->pat);
    upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
    upipe_ts_patd_clean_programs(upipe);
    upipe_ts_patd_clean_output(upipe);
    upipe_ts_patd_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_patd_mgr = {
    .signature = UPIPE_TS_PATD_SIGNATURE,

    .upipe_alloc = upipe_ts_patd_alloc,
    .upipe_input = upipe_ts_patd_input,
    .upipe_control = upipe_ts_patd_control,
    .upipe_free = upipe_ts_patd_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_patd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_patd_mgr_alloc(void)
{
    return &upipe_ts_patd_mgr;
}

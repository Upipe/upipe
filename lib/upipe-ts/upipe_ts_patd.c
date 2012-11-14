/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_linear_output.h>
#include <upipe-ts/upipe_ts_patd.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psid.h"

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
    /** currently in effect PAT table */
    UPIPE_TS_PSID_TABLE_DECLARE(pat);
    /** PAT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_pat);
    /** current TSID */
    int tsid;
    /** input flow name */
    char *flow_name;
    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_patd, upipe)

/** @internal @This allocates a ts_patd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_patd_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           struct ulog *ulog)
{
    struct upipe_ts_patd *upipe_ts_patd =
        malloc(sizeof(struct upipe_ts_patd));
    if (unlikely(upipe_ts_patd == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_patd_to_upipe(upipe_ts_patd);
    upipe_init(upipe, uprobe, ulog);
    upipe->mgr = mgr; /* do not increment refcount as mgr is static */
    upipe->signature = UPIPE_TS_PATD_SIGNATURE;
    upipe_ts_psid_table_init(upipe_ts_patd->pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
    upipe_ts_patd->tsid = -1;
    upipe_ts_patd->flow_name = NULL;
    urefcount_init(&upipe_ts_patd->refcount);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sends the patd_tsid event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param tsid new TSID
 */
static void upipe_ts_patd_tsid(struct upipe *upipe, struct uref *uref,
                               unsigned int tsid)
{
    upipe_throw(upipe, UPROBE_TS_PATD_TSID, UPIPE_TS_PATD_SIGNATURE,
                uref, tsid);
}

/** @internal @This sends the patd_new_program event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param program program number (a.k.a. service ID)
 * @param control control packet describing the PMT of the program
 */
static void upipe_ts_patd_new_program(struct upipe *upipe, struct uref *uref,
                                      unsigned int program,
                                      struct uref *control)
{
    upipe_throw(upipe, UPROBE_TS_PATD_NEW_PROGRAM, UPIPE_TS_PATD_SIGNATURE,
                uref, program, control);
}

/** @internal @This sends the patd_del_program event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param program program number (a.k.a. service ID)
 */
static void upipe_ts_patd_del_program(struct upipe *upipe, struct uref *uref,
                                      unsigned int program)
{
    upipe_throw(upipe, UPROBE_TS_PATD_DEL_PROGRAM, UPIPE_TS_PATD_SIGNATURE,
                uref, program);
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
            ret = uref_block_peek_unmap(section, 0, PAT_PROGRAM_SIZE,       \
                                        program_buffer, pat_program);       \
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
            ulog_aerror(upipe->ulog);                                       \
            upipe_throw_aerror(upipe);                                      \
            break;                                                          \
        }                                                                   \
    }

/** @internal @This compares a PAT program with the current PAT.
 *
 * @param upipe description structure of the pipe
 * @param wanted_pat_program pointer to the program description in the PAT
 * @return false if there is no such program, or it has a different description
 */
static bool upipe_ts_patd_table_compare_program(struct upipe *upipe,
                                            const uint8_t *wanted_pat_program)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    if (!upipe_ts_psid_table_validate(upipe_ts_patd->pat))
        return false;

    UPIPE_TS_PATD_TABLE_PEEK(upipe, upipe_ts_patd->pat, pat_program)

    uint16_t program = patn_get_program(pat_program);
    bool compare = !memcmp(pat_program, wanted_pat_program, PAT_PROGRAM_SIZE);

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, upipe_ts_patd->pat, pat_program)

    if (program == patn_get_program(wanted_pat_program))
        return compare;

    UPIPE_TS_PATD_TABLE_PEEK_END(upipe, upipe_ts_patd->pat, pat_program)
    return false;
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
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        uref_free(uref);
        return;
    }
    bool validate = pat_validate(pat_header);
    uint16_t tsid = psi_get_tableidext(pat_header);
    bool current = psi_get_current(pat_header);
    ret = uref_block_peek_unmap(uref, 0, PAT_HEADER_SIZE, buffer, pat_header);
    assert(ret);

    if (unlikely(!validate)) {
        ulog_warning(upipe->ulog, "invalid PAT section received");
        uref_free(uref);
        return;
    }

    if (!current) {
        /* Ignore sections which are not in use yet. */
        uref_free(uref);
        return;
    }

    if (unlikely(tsid != upipe_ts_patd->tsid)) {
        upipe_ts_patd_tsid(upipe, uref, tsid);
        upipe_ts_patd->tsid = tsid;
    }

    if (!upipe_ts_psid_table_section(upipe_ts_patd->next_pat, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_patd->pat) &&
        upipe_ts_psid_table_compare(upipe_ts_patd->pat,
                                    upipe_ts_patd->next_pat)) {
        /* Identical PAT. */
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
        return;
    }

    if (!upipe_ts_patd_table_validate(upipe)) {
        ulog_warning(upipe->ulog, "invalid PAT section received");
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
        return;
    }

    UPIPE_TS_PATD_TABLE_PEEK(upipe, upipe_ts_patd->next_pat, pat_program)

    uint16_t program = patn_get_program(pat_program);
    uint16_t pid = patn_get_pid(pat_program);
    bool compare = upipe_ts_patd_table_compare_program(upipe, pat_program);

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, upipe_ts_patd->next_pat, pat_program)

    if (!compare) {
        struct uref *control = uref_dup(uref);
        if (unlikely(control == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return;
        }
        ubuf_free(uref_detach_ubuf(control));
        if (unlikely(!uref_flow_set_def(control,
                                        "block.mpegtspsi.mpegtspmt.") ||
                     !uref_ts_flow_set_pid(control, pid))) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            uref_free(control);
            return;
        }
        upipe_ts_patd_new_program(upipe, uref, program, control);
    }

    UPIPE_TS_PATD_TABLE_PEEK_END(upipe, upipe_ts_patd->next_pat, pat_program)

    /* Switch tables. */
    UPIPE_TS_PSID_TABLE_DECLARE(old_pat);
    upipe_ts_psid_table_copy(old_pat, upipe_ts_patd->pat);
    upipe_ts_psid_table_copy(upipe_ts_patd->pat, upipe_ts_patd->next_pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);

    if (upipe_ts_psid_table_validate(old_pat)) {
        UPIPE_TS_PATD_TABLE_PEEK(upipe, old_pat, pat_program)

        uint16_t program = patn_get_program(pat_program);

        UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, old_pat, pat_program)

        if (!upipe_ts_patd_table_pmt_pid(upipe, upipe_ts_patd->pat, program))
            upipe_ts_patd_del_program(upipe, uref, program);

        UPIPE_TS_PATD_TABLE_PEEK_END(upipe, old_pat, pat_program)

        upipe_ts_psid_table_clean(old_pat);
    }
}

/** @internal @This sets the source flow name.
 *
 * @param upipe description structure of the pipe
 * @param flow_name source flow name
 */
static void upipe_ts_patd_set_flow_name(struct upipe *upipe,
                                        const char *flow_name)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    free(upipe_ts_patd->flow_name);
    upipe_ts_patd->flow_name = strdup(flow_name);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @return false if the buffer couldn't be accepted
 */
static bool upipe_ts_patd_input(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);

    const char *flow, *def;
    if (unlikely(!uref_flow_get_name(uref, &flow))) {
       ulog_warning(upipe->ulog, "received a buffer outside of a flow");
       uref_free(uref);
       return false;
    }

    if (unlikely(uref_flow_get_delete(uref))) {
        upipe_ts_patd_set_flow_name(upipe, NULL);
        uref_free(uref);
        return true;
    }

    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(upipe_ts_patd->flow_name != NULL)) {
            ulog_warning(upipe->ulog,
                         "received flow definition without delete first");
            upipe_ts_patd_set_flow_name(upipe, NULL);
        }
        if (unlikely(strncmp(def, EXPECTED_FLOW_DEF,
                             strlen(EXPECTED_FLOW_DEF)))) {
            ulog_warning(upipe->ulog,
                         "received an incompatible flow definition");
            uref_free(uref);
            return false;
        }

        ulog_debug(upipe->ulog, "flow definition for %s: %s", flow, def);
        upipe_ts_patd_set_flow_name(upipe, flow);
        uref_free(uref);
        return true;
    }

    if (unlikely(upipe_ts_patd->flow_name == NULL)) {
        ulog_warning(upipe->ulog, "pipe has no registered input flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(strcmp(upipe_ts_patd->flow_name, flow))) {
        ulog_warning(upipe->ulog,
                     "received a buffer not matching the current flow");
        uref_free(uref);
        return false;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return true;
    }

    upipe_ts_patd_work(upipe, uref);
    return true;
}

/** @internal @This processes control commands on a ts patd pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_patd_control(struct upipe *upipe,
                                  enum upipe_command command, va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        return upipe_ts_patd_input(upipe, uref);
    }

    return false;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_patd_use(struct upipe *upipe)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    urefcount_use(&upipe_ts_patd->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_patd_release(struct upipe *upipe)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_patd->refcount))) {
        upipe_ts_psid_table_clean(upipe_ts_patd->pat);
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        free(upipe_ts_patd->flow_name);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_patd->refcount);
        free(upipe_ts_patd);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_patd_mgr = {
    .upipe_alloc = upipe_ts_patd_alloc,
    .upipe_control = upipe_ts_patd_control,
    .upipe_use = upipe_ts_patd_use,
    .upipe_release = upipe_ts_patd_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_patd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_patd_mgr_alloc(void)
{
    return &upipe_ts_patd_mgr;
}

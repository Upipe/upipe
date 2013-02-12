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
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
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
    /** currently in effect PAT table */
    UPIPE_TS_PSID_TABLE_DECLARE(pat);
    /** PAT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_pat);
    /** current TSID */
    int tsid;
    /** true if we received a compatible flow definition */
    bool flow_def_ok;

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
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_patd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe)
{
    struct upipe_ts_patd *upipe_ts_patd =
        malloc(sizeof(struct upipe_ts_patd));
    if (unlikely(upipe_ts_patd == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_patd_to_upipe(upipe_ts_patd);
    upipe_init(upipe, mgr, uprobe);
    upipe_ts_psid_table_init(upipe_ts_patd->pat);
    upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
    upipe_ts_patd->tsid = -1;
    upipe_ts_patd->flow_def_ok = false;
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

/** @internal @This sends the patd_add_program event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param program program number (a.k.a. service ID)
 * @param pid PID of the PMT
 */
static void upipe_ts_patd_add_program(struct upipe *upipe, struct uref *uref,
                                      unsigned int program,
                                      unsigned int pid)
{
    upipe_throw(upipe, UPROBE_TS_PATD_ADD_PROGRAM, UPIPE_TS_PATD_SIGNATURE,
                uref, program, pid);
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
            ret = uref_block_peek_unmap(section, offset, PAT_PROGRAM_SIZE,  \
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
            upipe_throw_aerror(upipe);                                      \
            break;                                                          \
        }                                                                   \
    }

/** @internal @This compares a PAT program with the current PAT.
 *
 * @param upipe description structure of the pipe
 * @param wanted_pat_program pointer to the program description in the PAT
 * @param pid_p filled in with the PMT PID
 * @return false if there is no such program, or it has a different description
 */
static bool upipe_ts_patd_table_compare_program(struct upipe *upipe,
                                            const uint8_t *wanted_pat_program,
                                            uint16_t *pid_p)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    *pid_p = 0;
    if (!upipe_ts_psid_table_validate(upipe_ts_patd->pat))
        return false;

    uint16_t wanted_program = patn_get_program(wanted_pat_program);
    uint16_t wanted_pid = patn_get_pid(wanted_pat_program);

    UPIPE_TS_PATD_TABLE_PEEK(upipe, upipe_ts_patd->pat, pat_program)

    uint16_t program = patn_get_program(pat_program);
    uint16_t pid = patn_get_pid(pat_program);
    bool compare = pid == wanted_pid;

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, upipe_ts_patd->pat, pat_program)

    if (program == wanted_program) {
        *pid_p = pid;
        return compare;
    }

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
        upipe_warn(upipe, "invalid PAT section received");
        uref_free(uref);
        return;
    }
    bool validate = pat_validate(pat_header);
    uint16_t tsid = psi_get_tableidext(pat_header);
    ret = uref_block_peek_unmap(uref, 0, PAT_HEADER_SIZE, buffer, pat_header);
    assert(ret);

    if (unlikely(!validate)) {
        upipe_warn(upipe, "invalid PAT section received");
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
        upipe_warn(upipe, "invalid PAT section received");
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);
        upipe_ts_psid_table_init(upipe_ts_patd->next_pat);
        return;
    }

    UPIPE_TS_PATD_TABLE_PEEK(upipe, upipe_ts_patd->next_pat, pat_program)

    uint16_t program = patn_get_program(pat_program);
    uint16_t pid = patn_get_pid(pat_program);
    uint16_t old_pid;
    bool compare = upipe_ts_patd_table_compare_program(upipe, pat_program,
                                                       &old_pid);

    UPIPE_TS_PATD_TABLE_PEEK_UNMAP(upipe, upipe_ts_patd->next_pat, pat_program)

    if (!compare)
        upipe_ts_patd_add_program(upipe, uref, program, pid);

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

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_patd_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_patd *upipe_ts_patd = upipe_ts_patd_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            uref_free(uref);
            upipe_ts_patd->flow_def_ok = false;
            upipe_throw_flow_def_error(upipe, uref);
            return;
        }

        upipe_dbg_va(upipe, "flow definition: %s", def);
        upipe_ts_patd->flow_def_ok = true;
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_ts_patd->flow_def_ok)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_ts_patd_work(upipe, uref);
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
        upipe_throw_dead(upipe);

        upipe_ts_psid_table_clean(upipe_ts_patd->pat);
        upipe_ts_psid_table_clean(upipe_ts_patd->next_pat);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_patd->refcount);
        free(upipe_ts_patd);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_patd_mgr = {
    .signature = UPIPE_TS_PATD_SIGNATURE,

    .upipe_alloc = upipe_ts_patd_alloc,
    .upipe_input = upipe_ts_patd_input,
    .upipe_control = NULL,
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

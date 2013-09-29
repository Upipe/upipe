/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short Upipe module generating PSI tables
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_psi_generator.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>

/** @internal @This is the private context of a ts psig pipe. */
struct upipe_ts_psig {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** TS ID */
    uint16_t tsid;
    /** PAT version */
    uint8_t pat_version;

    /** list of program subpipes */
    struct uchain programs;

    /** manager to create program subpipes */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig, upipe, UPIPE_TS_PSIG_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_psig, "void.")
UPIPE_HELPER_UBUF_MGR(upipe_ts_psig, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_ts_psig, output, flow_def, flow_def_sent)

/** @internal @This is the private context of a program of a ts_psig pipe. */
struct upipe_ts_psig_program {
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** program number (service ID) */
    uint16_t program_number;
    /** PMT PID */
    uint16_t pmt_pid;
    /** PMT version */
    uint8_t pmt_version;
    /** PCR PID */
    uint16_t pcr_pid;
    /** descriptors */
    const uint8_t *descriptors;
    /** descriptors size */
    size_t descriptors_size;

    /** list of flow subpipes */
    struct uchain flows;

    /** manager to create flow subpipes */
    struct upipe_mgr flow_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig_program, upipe, UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_psig_program, "void.")
UPIPE_HELPER_OUTPUT(upipe_ts_psig_program, output, flow_def, flow_def_sent)

UPIPE_HELPER_SUBPIPE(upipe_ts_psig, upipe_ts_psig_program, program, program_mgr,
                     programs, uchain)

/** @internal @This is the private context of an elementary stream of a
 * ts_psig pipe. */
struct upipe_ts_psig_flow {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** input flow definition */
    struct uref *flow_def_input;

    /** PID */
    uint16_t pid;
    /** stream type */
    uint8_t stream_type;
    /** descriptors */
    const uint8_t *descriptors;
    /** descriptors size */
    size_t descriptors_size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig_flow, upipe, UPIPE_TS_PSIG_FLOW_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_psig_flow, "void.")

UPIPE_HELPER_SUBPIPE(upipe_ts_psig_program, upipe_ts_psig_flow, flow, flow_mgr,
                     flows, uchain)

/** @internal @This allocates a flow of a ts_psig_program pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psig_flow_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t signature,
                                              va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psig_flow_alloc_flow(mgr, uprobe, signature,
                                                        args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint64_t stream_type, pid;
    if (unlikely(!uref_ts_flow_get_stream_type(flow_def, &stream_type) ||
                 !uref_ts_flow_get_pid(flow_def, &pid))) {
        uref_free(flow_def);
        upipe_ts_psig_flow_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_psig_flow *upipe_ts_psig_flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    upipe_ts_psig_flow->pid = pid;
    upipe_ts_psig_flow->stream_type = stream_type;
    upipe_ts_psig_flow->descriptors = NULL;
    upipe_ts_psig_flow->descriptors_size = 0;
    uref_ts_flow_get_descriptors(flow_def, &upipe_ts_psig_flow->descriptors,
                                 &upipe_ts_psig_flow->descriptors_size);
    upipe_ts_psig_flow->flow_def_input = flow_def;
    upipe_ts_psig_flow_init_sub(upipe);

    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
    upipe_use(upipe_ts_psig_program_to_upipe(upipe_ts_psig_program));
    upipe_ts_psig_program->pmt_version++;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psig_flow_control(struct upipe *upipe,
                                       enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_flow_get_super(upipe, p);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_flow_free(struct upipe *upipe)
{
    struct upipe_ts_psig_flow *upipe_ts_psig_flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    uref_free(upipe_ts_psig_flow->flow_def_input);
    upipe_ts_psig_flow_clean_sub(upipe);
    upipe_ts_psig_flow_free_flow(upipe);

    upipe_release(upipe_ts_psig_program_to_upipe(upipe_ts_psig_program));
}

/** @internal @This initializes the flow manager for a ts_psig_program pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_init_flow_mgr(struct upipe *upipe)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_mgr *flow_mgr = &upipe_ts_psig_program->flow_mgr;
    flow_mgr->signature = UPIPE_TS_PSIG_FLOW_SIGNATURE;
    flow_mgr->upipe_alloc = upipe_ts_psig_flow_alloc;
    flow_mgr->upipe_input = NULL;
    flow_mgr->upipe_control = upipe_ts_psig_flow_control;
    flow_mgr->upipe_free = upipe_ts_psig_flow_free;
    flow_mgr->upipe_mgr_free = NULL;
}

/** @internal @This allocates a program of a ts_psig pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psig_program_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature,
                                                 va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psig_program_alloc_flow(mgr, uprobe,
                                                           signature,
                                                           args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint64_t program_number, pid;
    if (unlikely(!uref_flow_set_def(flow_def,
                                    "block.mpegtspsi.mpegtspmt.") ||
                 !uref_ts_flow_get_sid(flow_def, &program_number) ||
                 !uref_ts_flow_get_pid(flow_def, &pid))) {
        uref_free(flow_def);
        upipe_ts_psig_program_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program_init_output(upipe);
    upipe_ts_psig_program_init_flow_mgr(upipe);
    upipe_ts_psig_program_init_sub_flows(upipe);
    upipe_ts_psig_program->program_number = program_number;
    upipe_ts_psig_program->pmt_pid = pid;
    upipe_ts_psig_program->pmt_version = 0;
    uref_ts_flow_get_psi_version(flow_def, &upipe_ts_psig_program->pmt_version);
    upipe_ts_psig_program->pcr_pid = 8191;
    upipe_ts_psig_program->descriptors = NULL;
    upipe_ts_psig_program->descriptors_size = 0;
    uref_ts_flow_get_descriptors(flow_def, &upipe_ts_psig_program->descriptors,
                                 &upipe_ts_psig_program->descriptors_size);
    upipe_ts_psig_program_store_flow_def(upipe, flow_def);
    upipe_ts_psig_program_init_sub(upipe);

    struct upipe_ts_psig *upipe_ts_psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    upipe_use(upipe_ts_psig_to_upipe(upipe_ts_psig));
    upipe_ts_psig->pat_version++;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This generates a PMT PSI section, using the uref received.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_psig_program_input(struct upipe *upipe, struct uref *uref,
                                        struct upump *upump)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *upipe_ts_psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    if (unlikely(upipe_ts_psig->ubuf_mgr == NULL))
        upipe_throw_need_ubuf_mgr(upipe, upipe_ts_psig->flow_def);
    if (unlikely(upipe_ts_psig->ubuf_mgr == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_notice_va(upipe,
                    "new PMT program=%"PRIu16" version=%"PRIu8" pcrpid=%"PRIu16,
                    upipe_ts_psig_program->program_number,
                    upipe_ts_psig_program->pmt_version,
                    upipe_ts_psig_program->pcr_pid);

    struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_psig->ubuf_mgr,
                                         PSI_MAX_SIZE + PSI_HEADER_SIZE);
    if (unlikely(ubuf == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int size = -1;
    if (!ubuf_block_write(ubuf, 0, &size, &buffer)) {
        ubuf_free(ubuf);
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    pmt_init(buffer);
    /* set length later */
    psi_set_length(buffer, PSI_MAX_SIZE);
    pmt_set_program(buffer, upipe_ts_psig_program->program_number);
    psi_set_version(buffer, upipe_ts_psig_program->pmt_version);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, upipe_ts_psig_program->pcr_pid);
    uint8_t *descs = pmt_get_descs(buffer);
    descs_set_length(descs, upipe_ts_psig_program->descriptors_size);
    if (upipe_ts_psig_program->descriptors_size) {
        memcpy(descs_get_desc(descs, 0), upipe_ts_psig_program->descriptors,
               upipe_ts_psig_program->descriptors_size);
        if (!descs_validate(descs)) {
            upipe_warn(upipe, "invalid PMT descriptor loop");
            upipe_throw_error(upipe, UPROBE_ERR_INVALID);
        }
    }

    uint16_t j = 0;
    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psig_program->flows, uchain) {
        struct upipe_ts_psig_flow *upipe_ts_psig_flow =
            upipe_ts_psig_flow_from_uchain(uchain);
        upipe_notice_va(upipe, " * ES pid=%"PRIu16" streamtype=0x%"PRIx8,
                        upipe_ts_psig_flow->pid,
                        upipe_ts_psig_flow->stream_type);

        uint8_t *es = pmt_get_es(buffer, j);
        if (unlikely(es == NULL ||
                     !pmt_validate_es(buffer, es,
                                      upipe_ts_psig_flow->descriptors_size))) {
            upipe_warn(upipe, "PMT too large");
            upipe_throw_error(upipe, UPROBE_ERR_INVALID);
            break;
        }

        pmtn_init(es);
        pmtn_set_streamtype(es, upipe_ts_psig_flow->stream_type);
        pmtn_set_pid(es, upipe_ts_psig_flow->pid);
        descs = pmtn_get_descs(es);
        descs_set_length(descs, upipe_ts_psig_flow->descriptors_size);
        if (upipe_ts_psig_flow->descriptors_size) {
            memcpy(descs_get_desc(descs, 0), upipe_ts_psig_flow->descriptors,
                   upipe_ts_psig_flow->descriptors_size);
            if (!descs_validate(descs)) {
                upipe_warn(upipe, "invalid ES descriptor loop");
                upipe_throw_error(upipe, UPROBE_ERR_INVALID);
            }
        }
        j++;
    }

    uint8_t *es = pmt_get_es(buffer, j);
    pmt_set_length(buffer, es - buffer - PMT_HEADER_SIZE);
    uint16_t pmt_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
    psi_set_crc(buffer);
    ubuf_block_unmap(ubuf, 0);

    ubuf_block_resize(ubuf, 0, pmt_size);
    uref_attach_ubuf(uref, ubuf);
    uref_block_set_start(uref);
    upipe_ts_psig_program_output(upipe, uref, upump);

    upipe_notice(upipe, "end PMT");
}

/** @internal @This returns the current PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid_p filled in with the pcr_pid
 * @return false in case of error
 */
static bool _upipe_ts_psig_program_get_pcr_pid(struct upipe *upipe,
                                               unsigned int *pcr_pid_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(pcr_pid_p != NULL);
    *pcr_pid_p = upipe_ts_psig_program->pcr_pid;
    return true;
}

/** @internal @This sets the PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid pcr_pid
 * @return false in case of error
 */
static bool _upipe_ts_psig_program_set_pcr_pid(struct upipe *upipe,
                                               unsigned int pcr_pid)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program->pcr_pid = pcr_pid;
    return true;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psig_program_control(struct upipe *upipe,
                                          enum upipe_command command,
                                          va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psig_program_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_program_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_psig_program_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_psig_program_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_program_iterate_sub(upipe, p);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_program_get_super(upipe, p);
        }

        case UPIPE_TS_PSIG_PROGRAM_GET_PCR_PID: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PSIG_PROGRAM_SIGNATURE);
            unsigned int *pcr_pid_p = va_arg(args, unsigned int *);
            return _upipe_ts_psig_program_get_pcr_pid(upipe, pcr_pid_p);
        }
        case UPIPE_TS_PSIG_PROGRAM_SET_PCR_PID: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_PSIG_PROGRAM_SIGNATURE);
            unsigned int pcr_pid = va_arg(args, unsigned int);
            return _upipe_ts_psig_program_set_pcr_pid(upipe, pcr_pid);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_free(struct upipe *upipe)
{
    struct upipe_ts_psig *upipe_ts_psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_ts_psig_program_clean_sub_flows(upipe);
    upipe_ts_psig_program_clean_sub(upipe);
    upipe_ts_psig_program_clean_output(upipe);
    upipe_ts_psig_program_free_flow(upipe);

    upipe_release(upipe_ts_psig_to_upipe(upipe_ts_psig));
}

/** @internal @This initializes the program manager for a ts_psig pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_psig->program_mgr;
    program_mgr->signature = UPIPE_TS_PSIG_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_psig_program_alloc;
    program_mgr->upipe_input = upipe_ts_psig_program_input;
    program_mgr->upipe_control = upipe_ts_psig_program_control;
    program_mgr->upipe_free = upipe_ts_psig_program_free;
    program_mgr->upipe_mgr_free = NULL;
}

/** @internal @This allocates a ts_psig pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_psig_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_psig_alloc_flow(mgr, uprobe, signature,
                                                   args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    uint64_t tsid;

    if (unlikely(!uref_flow_set_def(flow_def,
                                    "block.mpegtspsi.mpegtspat.") ||
                 !uref_ts_flow_get_tsid(flow_def, &tsid))) {
        uref_free(flow_def);
        upipe_ts_psig_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_ts_psig_init_ubuf_mgr(upipe);
    upipe_ts_psig_init_output(upipe);
    upipe_ts_psig_init_program_mgr(upipe);
    upipe_ts_psig_init_sub_programs(upipe);
    upipe_ts_psig->tsid = tsid;
    upipe_ts_psig->pat_version = 0;
    uref_ts_flow_get_psi_version(flow_def, &upipe_ts_psig->pat_version);
    upipe_ts_psig_store_flow_def(upipe, flow_def);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This generates a PAT PSI section, using the uref received.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_psig_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    if (unlikely(upipe_ts_psig->ubuf_mgr == NULL))
        upipe_throw_need_ubuf_mgr(upipe, upipe_ts_psig->flow_def);
    if (unlikely(upipe_ts_psig->ubuf_mgr == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_notice_va(upipe, "new PAT tsid=%"PRIu16" version=%"PRIu8,
                    upipe_ts_psig->tsid, upipe_ts_psig->pat_version);

    unsigned int nb_sections = 0;
    struct uchain sections;
    ulist_init(&sections);
    struct uchain *program_chain = &upipe_ts_psig->programs;

    do {
        if (unlikely(nb_sections >= PSI_TABLE_MAX_SECTIONS)) {
            upipe_warn(upipe, "PAT too large");
            upipe_throw_error(upipe, UPROBE_ERR_INVALID);
            break;
        }

        struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_psig->ubuf_mgr,
                                             PSI_MAX_SIZE + PSI_HEADER_SIZE);
        if (unlikely(ubuf == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        uint8_t *buffer;
        int size = -1;
        if (!ubuf_block_write(ubuf, 0, &size, &buffer)) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        pat_init(buffer);
        /* set length later */
        psi_set_length(buffer, PSI_MAX_SIZE);
        pat_set_tsid(buffer, upipe_ts_psig->tsid);
        psi_set_version(buffer, upipe_ts_psig->pat_version);
        psi_set_current(buffer);
        psi_set_section(buffer, nb_sections);
        /* set last section in the end */

        uint16_t j = 0;
        uint8_t *program;
        while ((program = pat_get_program(buffer, j)) != NULL &&
               !ulist_is_last(&upipe_ts_psig->programs, program_chain)) {
            program_chain = program_chain->next;
            j++;

            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_uchain(program_chain);
            upipe_notice_va(upipe, " * program number=%"PRIu16" pid=%"PRIu16,
                            upipe_ts_psig_program->program_number,
                            upipe_ts_psig_program->pmt_pid);

            patn_init(program);
            patn_set_program(program, upipe_ts_psig_program->program_number);
            patn_set_pid(program, upipe_ts_psig_program->pmt_pid);
        }

        pat_set_length(buffer, program - buffer - PAT_HEADER_SIZE);
        uint16_t pat_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);

        ubuf_block_resize(ubuf, 0, pat_size);
        ulist_add(&sections, ubuf_to_uchain(ubuf));
        nb_sections++;
    } while (!ulist_is_last(&upipe_ts_psig->programs, program_chain));

    upipe_notice_va(upipe, "end PAT (%u sections)", nb_sections);

    struct uchain *section_chain;
    bool first = true;
    while ((section_chain = ulist_pop(&sections)) != NULL) {
        bool last = ulist_empty(&sections);
        struct ubuf *ubuf = ubuf_from_uchain(section_chain);
        uint8_t *buffer;
        int size = -1;
        if (!ubuf_block_write(ubuf, 0, &size, &buffer)) {
            if (last)
                uref_free(uref);
            ubuf_free(ubuf);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            continue;
        }

        psi_set_lastsection(buffer, nb_sections - 1);
        psi_set_crc(buffer);

        ubuf_block_unmap(ubuf, 0);

        struct uref *output;
        if (last)
            output = uref;
        else {
            output = uref_dup(uref);
            if (unlikely(output == NULL)) {
                ubuf_free(ubuf);
                upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
                continue;
            }
        }
        uref_attach_ubuf(output, ubuf);
        if (first)
            uref_block_set_start(uref);
        upipe_ts_psig_output(upipe, output, upump);
        first = false;
    }
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_psig_control(struct upipe *upipe,
                                  enum upipe_command command,
                                  va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_ts_psig_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_ts_psig_set_ubuf_mgr(upipe, ubuf_mgr);
        }

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psig_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_psig_set_output(upipe, output);
        }
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_psig_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_iterate_sub(upipe, p);
        }

        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_ts_psig_clean_sub_programs(upipe);
    upipe_ts_psig_clean_output(upipe);
    upipe_ts_psig_clean_ubuf_mgr(upipe);
    upipe_ts_psig_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_psig_mgr = {
    .signature = UPIPE_TS_PSIG_SIGNATURE,

    .upipe_alloc = upipe_ts_psig_alloc,
    .upipe_input = upipe_ts_psig_input,
    .upipe_control = upipe_ts_psig_control,
    .upipe_free = upipe_ts_psig_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_psig pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psig_mgr_alloc(void)
{
    return &upipe_ts_psig_mgr;
}

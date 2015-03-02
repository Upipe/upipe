/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe-ts/upipe_ts_psi_generator.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>

/** @hidden */
static int upipe_ts_psig_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts psig pipe. */
struct upipe_ts_psig {
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

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** TS ID */
    uint16_t tsid;
    /** PAT version */
    uint8_t pat_version;
    /** PAT interval */
    uint64_t pat_interval;
    /** PAT sections */
    struct uchain pat_sections;
    /** PAT last cr_sys */
    uint64_t pat_cr_sys;
    /** true if the PAT update was frozen */
    bool frozen;

    /** list of program subpipes */
    struct uchain programs;

    /** manager to create program subpipes */
    struct upipe_mgr program_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig, upipe, UPIPE_TS_PSIG_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psig, urefcount, upipe_ts_psig_free)
UPIPE_HELPER_VOID(upipe_ts_psig)
UPIPE_HELPER_OUTPUT(upipe_ts_psig, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_ts_psig, uref_mgr, uref_mgr_request,
                      upipe_ts_psig_check,
                      upipe_ts_psig_register_output_request,
                      upipe_ts_psig_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_ts_psig, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_psig_check,
                      upipe_ts_psig_register_output_request,
                      upipe_ts_psig_unregister_output_request)

/** @hidden */
static void upipe_ts_psig_build(struct upipe *upipe);

/** @internal @This is the private context of a program of a ts_psig pipe. */
struct upipe_ts_psig_program {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** program number (service ID) */
    uint16_t program_number;
    /** PMT PID */
    uint16_t pmt_pid;
    /** PMT version */
    uint8_t pmt_version;
    /** PCR PID */
    uint16_t pcr_pid;
    /** descriptors */
    uint8_t *descriptors;
    /** descriptors size */
    size_t descriptors_size;
    /** PMT interval */
    uint64_t pmt_interval;
    /** PMT section */
    struct ubuf *pmt_section;
    /** PMT last cr_sys */
    uint64_t pmt_cr_sys;
    /** true if the PMT update was frozen */
    bool frozen;

    /** list of flow subpipes */
    struct uchain flows;

    /** manager to create flow subpipes */
    struct upipe_mgr flow_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig_program, upipe,
                   UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psig_program, urefcount,
                       upipe_ts_psig_program_free)
UPIPE_HELPER_VOID(upipe_ts_psig_program)
UPIPE_HELPER_OUTPUT(upipe_ts_psig_program, output, flow_def, output_state, request_list)

UPIPE_HELPER_SUBPIPE(upipe_ts_psig, upipe_ts_psig_program, program, program_mgr,
                     programs, uchain)

/** @hidden */
static void upipe_ts_psig_program_build(struct upipe *upipe);

/** @internal @This is the private context of an elementary stream of a
 * ts_psig pipe. */
struct upipe_ts_psig_flow {
    /** refcount management structure */
    struct urefcount urefcount;
    /** structure for double-linked lists */
    struct uchain uchain;
    /** input flow definition */
    struct uref *flow_def_input;

    /** PID */
    uint16_t pid;
    /** stream type */
    uint8_t stream_type;
    /** descriptors */
    uint8_t *descriptors;
    /** descriptors size */
    size_t descriptors_size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_psig_flow, upipe, UPIPE_TS_PSIG_FLOW_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_psig_flow, urefcount, upipe_ts_psig_flow_free)
UPIPE_HELPER_VOID(upipe_ts_psig_flow)

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
    struct upipe *upipe = upipe_ts_psig_flow_alloc_void(mgr, uprobe, signature,
                                                        args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psig_flow *upipe_ts_psig_flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    upipe_ts_psig_flow_init_urefcount(upipe);
    upipe_ts_psig_flow->pid = 8192;
    upipe_ts_psig_flow->stream_type = 0;
    upipe_ts_psig_flow->descriptors = NULL;
    upipe_ts_psig_flow->descriptors_size = 0;
    upipe_ts_psig_flow->flow_def_input = NULL;
    upipe_ts_psig_flow_init_sub(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psig_flow_set_flow_def(struct upipe *upipe,
                                           struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uint64_t stream_type, pid;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    UBASE_RETURN(uref_ts_flow_get_stream_type(flow_def, &stream_type))
    UBASE_RETURN(uref_ts_flow_get_pid(flow_def, &pid))
    size_t descriptors_size = uref_ts_flow_size_descriptors(flow_def);
    uint8_t *descriptors = malloc(descriptors_size);
    uref_ts_flow_extract_descriptors(flow_def, descriptors);

    struct upipe_ts_psig_flow *upipe_ts_psig_flow =
        upipe_ts_psig_flow_from_upipe(upipe);
    if (stream_type != upipe_ts_psig_flow->stream_type ||
        pid != upipe_ts_psig_flow->pid ||
        descriptors_size != upipe_ts_psig_flow->descriptors_size ||
        (descriptors_size &&
         memcmp(descriptors, upipe_ts_psig_flow->descriptors,
                descriptors_size))) {
        struct uref *flow_def_dup;
        if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
            free(descriptors);
            return UBASE_ERR_ALLOC;
        }
        if (upipe_ts_psig_flow->flow_def_input != NULL)
            uref_free(upipe_ts_psig_flow->flow_def_input);
        upipe_ts_psig_flow->flow_def_input = flow_def_dup;
        upipe_ts_psig_flow->pid = pid;
        upipe_ts_psig_flow->stream_type = stream_type;
        upipe_ts_psig_flow->descriptors_size = descriptors_size;
        free(upipe_ts_psig_flow->descriptors);
        upipe_ts_psig_flow->descriptors = descriptors;

        struct upipe_ts_psig_program *program =
            upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
        program->pmt_version++;
        program->pmt_version &= 0x1f;

        upipe_ts_psig_program_build(upipe_ts_psig_program_to_upipe(program));
    } else
        free(descriptors);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_psig_flow_control(struct upipe *upipe,
                                      int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe_ts_psig_program_to_upipe(upipe_ts_psig_program)->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_alloc_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe_ts_psig_program_to_upipe(upipe_ts_psig_program)->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_free_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psig_flow_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_psig_flow_get_super(upipe, p);
        }

        default:
            return UBASE_ERR_UNHANDLED;
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
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_flow_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    if (upipe_ts_psig_flow->flow_def_input != NULL)
        uref_free(upipe_ts_psig_flow->flow_def_input);
    free(upipe_ts_psig_flow->descriptors);
    upipe_ts_psig_flow_clean_sub(upipe);

    program->pmt_version++;
    program->pmt_version &= 0x1f;
    upipe_ts_psig_program_build(upipe_ts_psig_program_to_upipe(program));

    upipe_ts_psig_flow_clean_urefcount(upipe);
    upipe_ts_psig_flow_free_void(upipe);
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
    flow_mgr->refcount =
        upipe_ts_psig_program_to_urefcount(upipe_ts_psig_program);
    flow_mgr->signature = UPIPE_TS_PSIG_FLOW_SIGNATURE;
    flow_mgr->upipe_alloc = upipe_ts_psig_flow_alloc;
    flow_mgr->upipe_input = NULL;
    flow_mgr->upipe_control = upipe_ts_psig_flow_control;
    flow_mgr->upipe_mgr_control = NULL;
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
    struct upipe *upipe = upipe_ts_psig_program_alloc_void(mgr, uprobe,
                                                           signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program_init_urefcount(upipe);
    upipe_ts_psig_program_init_output(upipe);
    upipe_ts_psig_program_init_flow_mgr(upipe);
    upipe_ts_psig_program_init_sub_flows(upipe);
    upipe_ts_psig_program->program_number = 0;
    upipe_ts_psig_program->pmt_pid = 8192;
    upipe_ts_psig_program->pmt_version = 0;
    upipe_ts_psig_program->pcr_pid = 8191;
    upipe_ts_psig_program->descriptors = NULL;
    upipe_ts_psig_program->descriptors_size = 0;
    upipe_ts_psig_program->pmt_interval = 0;
    upipe_ts_psig_program->pmt_section = NULL;
    upipe_ts_psig_program->pmt_cr_sys = 0;
    upipe_ts_psig_program->frozen = false;
    upipe_ts_psig_program_init_sub(upipe);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This generates a new PMT PSI section.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_program_build(struct upipe *upipe)
{
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    if (unlikely(program->flow_def == NULL))
        return;
    if (unlikely(psig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding a PMT");
        return;
    }

    upipe_notice_va(upipe,
                    "new PMT program=%"PRIu16" version=%"PRIu8" pcrpid=%"PRIu16,
                    program->program_number, program->pmt_version,
                    program->pcr_pid);

    struct ubuf *ubuf = ubuf_block_alloc(psig->ubuf_mgr,
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

    pmt_init(buffer);
    /* set length later */
    psi_set_length(buffer, PSI_MAX_SIZE);
    pmt_set_program(buffer, program->program_number);
    psi_set_version(buffer, program->pmt_version);
    psi_set_current(buffer);
    pmt_set_pcrpid(buffer, program->pcr_pid);
    uint8_t *descs = pmt_get_descs(buffer);
    descs_set_length(descs, program->descriptors_size);
    if (program->descriptors_size) {
        memcpy(descs_get_desc(descs, 0), program->descriptors,
               program->descriptors_size);
        if (!descs_validate(descs)) {
            upipe_warn(upipe, "invalid PMT descriptor loop");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
        }
    }

    uint16_t j = 0;
    struct uchain *uchain;
    ulist_foreach (&program->flows, uchain) {
        struct upipe_ts_psig_flow *flow =
            upipe_ts_psig_flow_from_uchain(uchain);
        if (flow->pid == 8192)
            continue;
        upipe_notice_va(upipe, " * ES pid=%"PRIu16" streamtype=0x%"PRIx8,
                        flow->pid, flow->stream_type);

        uint8_t *es = pmt_get_es(buffer, j);
        if (unlikely(es == NULL ||
                     !pmt_validate_es(buffer, es, flow->descriptors_size))) {
            upipe_warn(upipe, "PMT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        pmtn_init(es);
        pmtn_set_streamtype(es, flow->stream_type);
        pmtn_set_pid(es, flow->pid);
        descs = pmtn_get_descs(es);
        descs_set_length(descs, flow->descriptors_size);
        if (flow->descriptors_size) {
            memcpy(descs_get_desc(descs, 0), flow->descriptors,
                   flow->descriptors_size);
            if (!descs_validate(descs)) {
                upipe_warn(upipe, "invalid ES descriptor loop");
                upipe_throw_error(upipe, UBASE_ERR_INVALID);
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

    ubuf_free(program->pmt_section);
    program->pmt_section = ubuf;
    program->pmt_cr_sys = 0;
    upipe_notice(upipe, "end PMT");

    struct uref *flow_def = program->flow_def;
    program->flow_def = NULL;
    uref_block_flow_set_size(flow_def, pmt_size);
    upipe_ts_psig_program_store_flow_def(upipe, flow_def);
}

/** @internal @This sends a PMT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_psig_program_send(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_psig_program *program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig =
        upipe_ts_psig_from_program_mgr(upipe->mgr);
    if (unlikely(psig->flow_def == NULL || psig->uref_mgr == NULL ||
                 program->pmt_section == NULL))
        return;

    program->pmt_cr_sys = cr_sys;
    upipe_verbose_va(upipe, "sending PMT (%"PRIu64")", cr_sys);

    struct uref *uref = uref_alloc(psig->uref_mgr);
    struct ubuf *ubuf = ubuf_dup(program->pmt_section);
    if (unlikely(uref == NULL || ubuf == NULL)) {
        uref_free(uref);
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    uref_attach_ubuf(uref, ubuf);
    uref_block_set_start(uref);
    uref_block_set_end(uref);
    uref_clock_set_cr_sys(uref, cr_sys);
    upipe_ts_psig_program_output(upipe, uref, NULL);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psig_program_set_flow_def(struct upipe *upipe,
                                              struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uint64_t program_number, pid;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    UBASE_RETURN(uref_flow_get_id(flow_def, &program_number))
    UBASE_RETURN(uref_ts_flow_get_pid(flow_def, &pid))
    size_t descriptors_size = uref_ts_flow_size_descriptors(flow_def);
    uint8_t *descriptors = malloc(descriptors_size);
    uref_ts_flow_extract_descriptors(flow_def, descriptors);

    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    if (program_number != upipe_ts_psig_program->program_number ||
        pid != upipe_ts_psig_program->pmt_pid ||
        descriptors_size != upipe_ts_psig_program->descriptors_size ||
        (descriptors_size &&
         memcmp(descriptors, upipe_ts_psig_program->descriptors,
                descriptors_size))) {
        struct uref *flow_def_dup;
        if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
            free(descriptors);
            return UBASE_ERR_ALLOC;
        }
        if (unlikely(!ubase_check(uref_flow_set_def(flow_def_dup,
                                        "block.mpegtspsi.mpegtspmt.")))) {
            free(descriptors);
            uref_free(flow_def);
            return UBASE_ERR_ALLOC;
        }

        upipe_ts_psig_program_store_flow_def(upipe, flow_def_dup);
        upipe_ts_psig_program->program_number = program_number;
        upipe_ts_psig_program->pmt_pid = pid;
        upipe_ts_psig_program->descriptors_size = descriptors_size;
        free(upipe_ts_psig_program->descriptors);
        upipe_ts_psig_program->descriptors = descriptors;

        struct upipe_ts_psig *psig = upipe_ts_psig_from_program_mgr(upipe->mgr);
        psig->pat_version++;
        psig->pat_version &= 0x1f;

        upipe_ts_psig_build(upipe_ts_psig_to_upipe(psig));
    } else
        free(descriptors);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid_p filled in with the pcr_pid
 * @return an error code
 */
static int _upipe_ts_psig_program_get_pcr_pid(struct upipe *upipe,
                                              unsigned int *pcr_pid_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(pcr_pid_p != NULL);
    *pcr_pid_p = upipe_ts_psig_program->pcr_pid;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PCR PID.
 *
 * @param upipe description structure of the pipe
 * @param pcr_pid pcr_pid
 * @return an error code
 */
static int _upipe_ts_psig_program_set_pcr_pid(struct upipe *upipe,
                                              unsigned int pcr_pid)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program->pcr_pid = pcr_pid;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_psig_program_get_pmt_interval(struct upipe *upipe,
                                                  uint64_t *interval_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_psig_program->pmt_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PMT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_psig_program_set_pmt_interval(struct upipe *upipe,
                                                  uint64_t interval)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_ts_psig_program->pmt_interval = interval;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current version.
 *
 * @param upipe description structure of the pipe
 * @param version_p filled in with the version
 * @return an error code
 */
static int upipe_ts_psig_program_get_version(struct upipe *upipe,
                                             unsigned int *version_p)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    assert(version_p != NULL);
    *version_p = upipe_ts_psig_program->pmt_version;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the version.
 *
 * @param upipe description structure of the pipe
 * @param version version
 * @return an error code
 */
static int upipe_ts_psig_program_set_version(struct upipe *upipe,
                                             unsigned int version)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    upipe_dbg_va(upipe, "setting version to %u\n", version);
    upipe_ts_psig_program->pmt_version = version;
    upipe_ts_psig_program->pmt_version &= 0x1f;
    upipe_ts_psig_program_build(upipe);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_psig_program_control(struct upipe *upipe,
                                         int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_alloc_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct upipe_ts_psig *upipe_ts_psig =
                upipe_ts_psig_from_program_mgr(upipe->mgr);
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_free_output_proxy(
                    upipe_ts_psig_to_upipe(upipe_ts_psig), request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psig_program_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psig_program_set_flow_def(upipe, flow_def);
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
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
            unsigned int *pcr_pid_p = va_arg(args, unsigned int *);
            return _upipe_ts_psig_program_get_pcr_pid(upipe, pcr_pid_p);
        }
        case UPIPE_TS_PSIG_PROGRAM_SET_PCR_PID: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PSIG_PROGRAM_SIGNATURE)
            unsigned int pcr_pid = va_arg(args, unsigned int);
            return _upipe_ts_psig_program_set_pcr_pid(upipe, pcr_pid);
        }
        case UPIPE_TS_MUX_GET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_psig_program_get_pmt_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PMT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_psig_program_set_pmt_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int *version_p = va_arg(args, unsigned int *);
            return upipe_ts_psig_program_get_version(upipe, version_p);
        }
        case UPIPE_TS_MUX_SET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int version = va_arg(args, unsigned int);
            return upipe_ts_psig_program_set_version(upipe, version);
        }
        case UPIPE_TS_MUX_FREEZE_PSI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_upipe(upipe);
            upipe_ts_psig_program->frozen = true;
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
static void upipe_ts_psig_program_free(struct upipe *upipe)
{
    struct upipe_ts_psig_program *upipe_ts_psig_program =
        upipe_ts_psig_program_from_upipe(upipe);
    struct upipe_ts_psig *psig = upipe_ts_psig_from_program_mgr(upipe->mgr);
    upipe_throw_dead(upipe);

    upipe_ts_psig_program_clean_sub_flows(upipe);
    upipe_ts_psig_program_clean_sub(upipe);
    upipe_ts_psig_program_clean_output(upipe);
    free(upipe_ts_psig_program->descriptors);
    ubuf_free(upipe_ts_psig_program->pmt_section);

    psig->pat_version++;
    psig->pat_version &= 0x1f;
    upipe_ts_psig_build(upipe_ts_psig_to_upipe(psig));

    upipe_ts_psig_program_clean_urefcount(upipe);
    upipe_ts_psig_program_free_void(upipe);
}

/** @internal @This initializes the program manager for a ts_psig pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_init_program_mgr(struct upipe *upipe)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    struct upipe_mgr *program_mgr = &upipe_ts_psig->program_mgr;
    program_mgr->refcount = upipe_ts_psig_to_urefcount(upipe_ts_psig);
    program_mgr->signature = UPIPE_TS_PSIG_PROGRAM_SIGNATURE;
    program_mgr->upipe_alloc = upipe_ts_psig_program_alloc;
    program_mgr->upipe_input = NULL;
    program_mgr->upipe_control = upipe_ts_psig_program_control;
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
    struct upipe *upipe = upipe_ts_psig_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_ts_psig_init_urefcount(upipe);
    upipe_ts_psig_init_uref_mgr(upipe);
    upipe_ts_psig_init_ubuf_mgr(upipe);
    upipe_ts_psig_init_output(upipe);
    upipe_ts_psig_init_program_mgr(upipe);
    upipe_ts_psig_init_sub_programs(upipe);
    upipe_ts_psig->tsid = 0;
    upipe_ts_psig->pat_version = 0;
    upipe_ts_psig->pat_interval = 0;
    ulist_init(&upipe_ts_psig->pat_sections);
    upipe_ts_psig->pat_cr_sys = 0;
    upipe_ts_psig->frozen = false;

    upipe_throw_ready(upipe);
    upipe_ts_psig_demand_uref_mgr(upipe);
    return upipe;
}

/** @internal @This builds new PAT PSI sections.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_build(struct upipe *upipe)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    if (unlikely(psig->flow_def == NULL))
        return;
    if (unlikely(psig->frozen)) {
        upipe_dbg_va(upipe, "not rebuilding a PAT");
        return;
    }

    upipe_notice_va(upipe, "new PAT tsid=%"PRIu16" version=%"PRIu8,
                    psig->tsid, psig->pat_version);

    unsigned int nb_sections = 0;
    struct uchain *program_chain = &psig->programs;
    uint64_t total_size = 0;

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&psig->pat_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));

    do {
        if (unlikely(nb_sections >= PSI_TABLE_MAX_SECTIONS)) {
            upipe_warn(upipe, "PAT too large");
            upipe_throw_error(upipe, UBASE_ERR_INVALID);
            break;
        }

        struct ubuf *ubuf = ubuf_block_alloc(psig->ubuf_mgr,
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

        pat_init(buffer);
        /* set length later */
        psi_set_length(buffer, PSI_MAX_SIZE);
        pat_set_tsid(buffer, psig->tsid);
        psi_set_version(buffer, psig->pat_version);
        psi_set_current(buffer);
        psi_set_section(buffer, nb_sections);
        /* set last section in the end */

        uint16_t j = 0;
        uint8_t *program;
        while ((program = pat_get_program(buffer, j)) != NULL &&
               !ulist_is_last(&psig->programs, program_chain)) {
            program_chain = program_chain->next;

            struct upipe_ts_psig_program *upipe_ts_psig_program =
                upipe_ts_psig_program_from_uchain(program_chain);
            if (upipe_ts_psig_program->pmt_pid == 8192)
                continue;
            upipe_notice_va(upipe, " * program number=%"PRIu16" pid=%"PRIu16,
                            upipe_ts_psig_program->program_number,
                            upipe_ts_psig_program->pmt_pid);

            j++;
            patn_init(program);
            patn_set_program(program, upipe_ts_psig_program->program_number);
            patn_set_pid(program, upipe_ts_psig_program->pmt_pid);
        }

        pat_set_length(buffer, program - buffer - PAT_HEADER_SIZE);
        uint16_t pat_size = psi_get_length(buffer) + PSI_HEADER_SIZE;
        ubuf_block_unmap(ubuf, 0);

        ubuf_block_resize(ubuf, 0, pat_size);
        ulist_add(&psig->pat_sections, ubuf_to_uchain(ubuf));
        nb_sections++;
        total_size += pat_size;
    } while (!ulist_is_last(&psig->programs, program_chain));

    ulist_foreach (&psig->pat_sections, section_chain) {
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

    psig->pat_cr_sys = 0;

    upipe_notice_va(upipe, "end PAT (%u sections)", nb_sections);

    struct uref *flow_def = psig->flow_def;
    psig->flow_def = NULL;
    uref_block_flow_set_size(flow_def, total_size);
    upipe_ts_psig_store_flow_def(upipe, flow_def);
}

/** @internal @This sends a PAT PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_psig_send(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    if (unlikely(psig->flow_def == NULL || ulist_empty(&psig->pat_sections)))
        return;

    psig->pat_cr_sys = cr_sys;
    upipe_verbose_va(upipe, "sending PAT (%"PRIu64")", cr_sys);

    struct uchain *section_chain;
    ulist_foreach (&psig->pat_sections, section_chain) {
        bool last = ulist_is_last(&psig->pat_sections, section_chain);
        struct ubuf *ubuf = ubuf_dup(ubuf_from_uchain(section_chain));
        struct uref *uref = uref_alloc(psig->uref_mgr);
        if (unlikely(uref == NULL || ubuf == NULL)) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        uref_attach_ubuf(uref, ubuf);
        uref_block_set_start(uref);
        if (last)
            uref_block_set_end(uref);
        uref_clock_set_cr_sys(uref, cr_sys);
        upipe_ts_psig_output(upipe, uref, NULL);
    }
}

/** @internal @This receives a ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_psig_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL)
        upipe_ts_psig_store_flow_def(upipe, flow_format);

    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    if (ulist_empty(&upipe_ts_psig->pat_sections))
        upipe_ts_psig_build(upipe);

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psig->programs, uchain) {
        struct upipe_ts_psig_program *program =
            upipe_ts_psig_program_from_uchain(uchain);
        if (program->pmt_section == NULL)
            upipe_ts_psig_program_build(upipe_ts_psig_program_to_upipe(program));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_psig_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    uint64_t tsid;
    UBASE_RETURN(uref_flow_match_def(flow_def, "void."))
    UBASE_RETURN(uref_flow_get_id(flow_def, &tsid))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def_dup,
                                         "block.mpegtspsi.mpegtspat."))
    upipe_ts_psig_store_flow_def(upipe, NULL);
    upipe_ts_psig_demand_ubuf_mgr(upipe, flow_def_dup);

    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_ts_psig->tsid = tsid;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval_p filled in with the interval
 * @return an error code
 */
static int upipe_ts_psig_get_pat_interval(struct upipe *upipe,
                                          uint64_t *interval_p)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    assert(interval_p != NULL);
    *interval_p = upipe_ts_psig->pat_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PAT interval.
 *
 * @param upipe description structure of the pipe
 * @param interval new interval
 * @return an error code
 */
static int upipe_ts_psig_set_pat_interval(struct upipe *upipe,
                                          uint64_t interval)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_ts_psig->pat_interval = interval;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current version.
 *
 * @param upipe description structure of the pipe
 * @param version_p filled in with the version
 * @return an error code
 */
static int upipe_ts_psig_get_version(struct upipe *upipe,
                                     unsigned int *version_p)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    assert(version_p != NULL);
    *version_p = upipe_ts_psig->pat_version;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the version.
 *
 * @param upipe description structure of the pipe
 * @param version version
 * @return an error code
 */
static int upipe_ts_psig_set_version(struct upipe *upipe,
                                     unsigned int version)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    upipe_dbg_va(upipe, "setting version to %u\n", version);
    upipe_ts_psig->pat_version = version;
    upipe_ts_psig->pat_version &= 0x1f;
    upipe_ts_psig_build(upipe);
    return UBASE_ERR_NONE;
}

/** @This prepares the next PSI sections for the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys current muxing date
 * @return an error code
 */
static int _upipe_ts_psig_prepare(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_psig *upipe_ts_psig = upipe_ts_psig_from_upipe(upipe);
    if (upipe_ts_psig->pat_cr_sys + upipe_ts_psig->pat_interval <= cr_sys)
        upipe_ts_psig_send(upipe, cr_sys);

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_psig->programs, uchain) {
        struct upipe_ts_psig_program *program =
            upipe_ts_psig_program_from_uchain(uchain);
        if (program->pmt_cr_sys + program->pmt_interval <= cr_sys)
            upipe_ts_psig_program_send(upipe_ts_psig_program_to_upipe(program),
                                       cr_sys);
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
static int upipe_ts_psig_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_psig_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_psig_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_psig_set_flow_def(upipe, flow_def);
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

        case UPIPE_TS_MUX_GET_PAT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *interval_p = va_arg(args, uint64_t *);
            return upipe_ts_psig_get_pat_interval(upipe, interval_p);
        }
        case UPIPE_TS_MUX_SET_PAT_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t interval = va_arg(args, uint64_t);
            return upipe_ts_psig_set_pat_interval(upipe, interval);
        }
        case UPIPE_TS_MUX_GET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int *version_p = va_arg(args, unsigned int *);
            return upipe_ts_psig_get_version(upipe, version_p);
        }
        case UPIPE_TS_MUX_SET_VERSION: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int version = va_arg(args, unsigned int);
            return upipe_ts_psig_set_version(upipe, version);
        }
        case UPIPE_TS_MUX_FREEZE_PSI: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
            psig->frozen = true;
            return UBASE_ERR_NONE;
        }

        case UPIPE_TS_PSIG_PREPARE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_PSIG_SIGNATURE)
            uint64_t cr_sys = va_arg(args, uint64_t);
            return _upipe_ts_psig_prepare(upipe, cr_sys);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_psig_free(struct upipe *upipe)
{
    struct upipe_ts_psig *psig = upipe_ts_psig_from_upipe(upipe);
    upipe_throw_dead(upipe);

    struct uchain *section_chain;
    while ((section_chain = ulist_pop(&psig->pat_sections)) != NULL)
        ubuf_free(ubuf_from_uchain(section_chain));
    upipe_ts_psig_clean_sub_programs(upipe);
    upipe_ts_psig_clean_output(upipe);
    upipe_ts_psig_clean_ubuf_mgr(upipe);
    upipe_ts_psig_clean_uref_mgr(upipe);
    upipe_ts_psig_clean_urefcount(upipe);
    upipe_ts_psig_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_psig_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_PSIG_SIGNATURE,

    .upipe_alloc = upipe_ts_psig_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_ts_psig_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_psig pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_psig_mgr_alloc(void)
{
    return &upipe_ts_psig_mgr;
}

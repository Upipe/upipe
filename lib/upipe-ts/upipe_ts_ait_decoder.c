/*
 * Copyright (C) 2024 EasyTools S.A.S.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the application information table of DVB
 * streams
 * Normative references:
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 *  - ETSI TS 102 809 V1.1.1 (2010-01) (Signalling and carriage of interactive
 *  applications and services)
 */

#include "upipe/ubase.h"
#include "upipe/uclock.h"
#include "upipe/ulist.h"
#include "upipe/uref.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe-ts/upipe_ts_ait_decoder.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtsait."
#define OUTPUT_FLOW_DEF "block.mpegtspsi.mpegtsait."

/** @hidden */
static int upipe_ts_aitd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_aitd pipe. */
struct upipe_ts_aitd {
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

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** currently in effect AIT table */
    UPIPE_TS_PSID_TABLE_DECLARE(ait);
    /** AIT table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_ait);

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_aitd, upipe, UPIPE_TS_AITD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_aitd, urefcount, upipe_ts_aitd_free)
UPIPE_HELPER_VOID(upipe_ts_aitd)
UPIPE_HELPER_OUTPUT(upipe_ts_aitd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_aitd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_aitd_check,
                      upipe_ts_aitd_register_output_request,
                      upipe_ts_aitd_unregister_output_request)
UPIPE_HELPER_UREF_MGR(upipe_ts_aitd, uref_mgr, uref_mgr_request, NULL,
                      upipe_ts_aitd_register_output_request,
                      upipe_ts_aitd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_aitd, flow_def_input, flow_def_attr)

/** @internal @This allocates a ts_aitd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_aitd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_aitd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_aitd *upipe_ts_aitd = upipe_ts_aitd_from_upipe(upipe);
    upipe_ts_aitd_init_urefcount(upipe);
    upipe_ts_aitd_init_output(upipe);
    upipe_ts_aitd_init_ubuf_mgr(upipe);
    upipe_ts_aitd_init_uref_mgr(upipe);
    upipe_ts_aitd_init_flow_def(upipe);
    upipe_ts_psid_table_init(upipe_ts_aitd->ait);
    upipe_ts_psid_table_init(upipe_ts_aitd->next_ait);

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This validates the next AIT.
 *
 * @param upipe description structure of the pipe
 * @return false if the AIT is invalid
 */
static bool upipe_ts_aitd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_aitd *upipe_ts_aitd = upipe_ts_aitd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_aitd->next_ait, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This outputs the AIT.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitd_send(struct upipe *upipe)
{
    struct upipe_ts_aitd *upipe_ts_aitd = upipe_ts_aitd_from_upipe(upipe);
    struct uref *uref = NULL;
    bool first = true;

    upipe_verbose(upipe, "send AIT");

    if (unlikely(upipe_ts_aitd->uref_mgr == NULL))
        return;

    upipe_use(upipe);

    upipe_ts_psid_table_foreach(upipe_ts_aitd->ait, section) {
        struct ubuf *ubuf = ubuf_dup(section->ubuf);
        if (uref)
            /* send previous section */
            upipe_ts_aitd_output(upipe, uref, NULL);
        uref = uref_alloc(upipe_ts_aitd->uref_mgr);

        if (unlikely(uref == NULL || ubuf == NULL)) {
            ubuf_free(ubuf);
            uref_free(uref);
            uref = NULL;
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            continue;
        }

        uref_attach_ubuf(uref, ubuf);
        if (first)
            uref_block_set_start(uref);
        first = false;
    }

    if (uref) {
        uref_block_set_end(uref);
        upipe_ts_aitd_output(upipe, uref, NULL);
    }

    upipe_release(upipe);
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_aitd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_aitd *upipe_ts_aitd = upipe_ts_aitd_from_upipe(upipe);
    assert(upipe_ts_aitd->flow_def_input != NULL);

    if (!upipe_ts_psid_table_section(upipe_ts_aitd->next_ait, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_aitd->ait) &&
        upipe_ts_psid_table_compare(upipe_ts_aitd->ait,
                                    upipe_ts_aitd->next_ait)) {
        /* Identical AIT. */
        upipe_ts_psid_table_clean(upipe_ts_aitd->next_ait);
        upipe_ts_psid_table_init(upipe_ts_aitd->next_ait);
        upipe_ts_aitd_send(upipe);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_aitd->next_ait,
                                               upipe_ts_aitd->ubuf_mgr)) ||
        !upipe_ts_aitd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid AIT section received");
        upipe_ts_psid_table_clean(upipe_ts_aitd->next_ait);
        upipe_ts_psid_table_init(upipe_ts_aitd->next_ait);
        return;
    }

    struct uref *flow_def = upipe_ts_aitd_alloc_flow_def_attr(upipe);
    if (flow_def)
        flow_def = upipe_ts_aitd_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_aitd_store_flow_def(upipe, flow_def);

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_aitd->ait))
        upipe_ts_psid_table_clean(upipe_ts_aitd->ait);
    upipe_ts_psid_table_copy(upipe_ts_aitd->ait, upipe_ts_aitd->next_ait);
    upipe_ts_psid_table_init(upipe_ts_aitd->next_ait);

    upipe_ts_aitd_send(upipe);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_aitd_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_ts_aitd *upipe_ts_aitd = upipe_ts_aitd_from_upipe(upipe);

    if (flow_format != NULL) {
        flow_format = upipe_ts_aitd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL)
            upipe_ts_aitd_store_flow_def(upipe, flow_format);
    }

    if (unlikely(upipe_ts_aitd->uref_mgr == NULL))
        upipe_ts_aitd_demand_uref_mgr(upipe);

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_aitd_set_flow_def(struct upipe *upipe,
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
    upipe_ts_aitd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_aitd_control_real(struct upipe *upipe, int command,
                                      va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_aitd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_aitd_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands and checks the internal state.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_aitd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(upipe_ts_aitd_control_real(upipe, command, args));
    return upipe_ts_aitd_check(upipe, NULL);
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_aitd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_aitd *upipe_ts_aitd = upipe_ts_aitd_from_upipe(upipe);
    upipe_ts_psid_table_clean(upipe_ts_aitd->ait);
    upipe_ts_psid_table_clean(upipe_ts_aitd->next_ait);
    upipe_ts_aitd_clean_output(upipe);
    upipe_ts_aitd_clean_uref_mgr(upipe);
    upipe_ts_aitd_clean_ubuf_mgr(upipe);
    upipe_ts_aitd_clean_flow_def(upipe);
    upipe_ts_aitd_clean_urefcount(upipe);
    upipe_ts_aitd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_aitd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_AITD_SIGNATURE,

    .upipe_alloc = upipe_ts_aitd_alloc,
    .upipe_input = upipe_ts_aitd_input,
    .upipe_control = upipe_ts_aitd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_aitd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_aitd_mgr_alloc(void)
{
    return &upipe_ts_aitd_mgr;
}

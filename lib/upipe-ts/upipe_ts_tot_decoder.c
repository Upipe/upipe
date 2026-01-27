/*
 * Copyright (C) 2023 EasyTools S.A.S.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/** @file
 * @short Upipe module decoding the time offset table of DVB streams
 * Normative references:
 *  - ETSI EN 300 468 V1.13.1 (2012-08) (SI in DVB systems)
 *  - ETSI TR 101 211 V1.9.1 (2009-06) (Guidelines of SI in DVB systems)
 */

#include "upipe/ubase.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_block.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_output.h"
#include "upipe/upipe_helper_ubuf_mgr.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe-ts/upipe_ts_tot_decoder.h"
#include "upipe-ts/uref_ts_flow.h"
#include "upipe_ts_psi_decoder.h"

#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>
#include <bitstream/dvb/si.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtstot."

/** @hidden */
static int upipe_ts_totd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_totd pipe. */
struct upipe_ts_totd {
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

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_totd, upipe, UPIPE_TS_TOTD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_totd, urefcount, upipe_ts_totd_free)
UPIPE_HELPER_VOID(upipe_ts_totd)
UPIPE_HELPER_OUTPUT(upipe_ts_totd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_totd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_totd_check,
                      upipe_ts_totd_register_output_request,
                      upipe_ts_totd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_totd, flow_def_input, flow_def_attr)

/** @internal @This allocates a ts_totd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_totd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_totd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_totd_init_urefcount(upipe);
    upipe_ts_totd_init_output(upipe);
    upipe_ts_totd_init_ubuf_mgr(upipe);
    upipe_ts_totd_init_flow_def(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_totd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_totd *upipe_ts_totd = upipe_ts_totd_from_upipe(upipe);
    assert(upipe_ts_totd->flow_def_input != NULL);
    assert(upipe_ts_totd->ubuf_mgr != NULL);

    const uint8_t *tot;
    int size = -1;
    if (unlikely(!ubase_check(uref_block_merge(uref, upipe_ts_totd->ubuf_mgr,
                                               0, -1)) ||
                 !ubase_check(uref_block_read(uref, 0, &size, &tot)))) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }

    /* tot_validate also checks CRC */
    if (unlikely(!tot_validate(tot))) {
        upipe_warn(upipe, "invalid TOT section received");
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return;
    }

    uint64_t utc = tot_get_utc(tot);

    struct uref *flow_def = upipe_ts_totd_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_block_unmap(uref, 0);
        uref_free(uref);
        return;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    const uint8_t *descs = tot_get_descs((uint8_t *)tot);
    const uint8_t *desc;
    int j = 0;

    while ((desc = descs_get_desc((uint8_t *)descs, j++)) != NULL) {
        if (unlikely(desc_get_tag(desc) != 0x58))
            continue;

        /* Local time descriptor */
        if (unlikely(!desc58_validate(desc))) {
            upipe_warn_va(upipe, "invalid descriptor 0x%x", desc_get_tag(desc));
            continue;
        }

        uint8_t k = 0;
        const uint8_t *lto;
        while ((lto = desc58_get_lto((uint8_t *)desc, k)) != NULL) {
            char code[4];
            memcpy(code, desc58n_get_country_code(lto), 3);
            code[3] = '\0';
            UBASE_FATAL(upipe, uref_ts_flow_set_tot_code(flow_def, code, k))

            uint8_t region = desc58n_get_country_region_id(lto);
            UBASE_FATAL(upipe, uref_ts_flow_set_tot_region(flow_def, region, k))

            int offset, hour, min;
            dvb_time_decode_bcd16(desc58n_get_lt_offset(lto), &offset,
                                  &hour, &min);
            offset *= desc58n_get_lto_polarity(lto) ? -1 : 1;
            UBASE_FATAL(upipe, uref_ts_flow_set_tot_offset(flow_def,
                (int64_t)offset * UCLOCK_FREQ, k))

            time_t time_of_change =
                dvb_time_decode_UTC(desc58n_get_time_of_change(lto));
            UBASE_FATAL(upipe, uref_ts_flow_set_tot_change(flow_def,
                (uint64_t)time_of_change * UCLOCK_FREQ, k))

            dvb_time_decode_bcd16(desc58n_get_next_offset(lto), &offset,
                                  &hour, &min);
            offset *= desc58n_get_lto_polarity(lto) ? -1 : 1;
            UBASE_FATAL(upipe, uref_ts_flow_set_tot_next_offset(flow_def,
                (uint64_t)offset * UCLOCK_FREQ, k))

            k++;
        }
        UBASE_FATAL(upipe, uref_ts_flow_set_tot_regions(flow_def, k))
    }

    uref_block_unmap(uref, 0);
    uref_free(uref);

    flow_def = upipe_ts_totd_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    upipe_ts_totd_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_totd_output(upipe, NULL, upump_p);

    char date[76];
    time_t time = dvb_time_format_UTC(utc, NULL, date);
    upipe_dbg_va(upipe, "throw UTC clock (%s)", date);
    upipe_throw_clock_utc(upipe, uref, UCLOCK_FREQ * time);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_totd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_totd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_totd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_totd_output(upipe, NULL, NULL);
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
static int upipe_ts_totd_set_flow_def(struct upipe *upipe,
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
    upipe_ts_totd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_totd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_totd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_totd_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_totd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_totd_clean_output(upipe);
    upipe_ts_totd_clean_ubuf_mgr(upipe);
    upipe_ts_totd_clean_flow_def(upipe);
    upipe_ts_totd_clean_urefcount(upipe);
    upipe_ts_totd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_totd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_TOTD_SIGNATURE,

    .upipe_alloc = upipe_ts_totd_alloc,
    .upipe_input = upipe_ts_totd_input,
    .upipe_control = upipe_ts_totd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_totd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_totd_mgr_alloc(void)
{
    return &upipe_ts_totd_mgr;
}

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
 * @short Upipe module decapsulating (removing TS header) TS packets
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_decaps.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegts."

/** @internal @This is the private context of a ts_decaps pipe. */
struct upipe_ts_decaps {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** last continuity counter for this PID, or -1 */
    int8_t last_cc;
    /** last TS packet */
    struct uref *last_uref;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_decaps, upipe, UPIPE_TS_DECAPS_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_decaps, urefcount, upipe_ts_decaps_free)
UPIPE_HELPER_VOID(upipe_ts_decaps)
UPIPE_HELPER_OUTPUT(upipe_ts_decaps, output, flow_def, output_state, request_list)

/** @internal @This allocates a ts_decaps pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_decaps_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_decaps_alloc_void(mgr, uprobe, signature,
                                                     args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_decaps *upipe_ts_decaps = upipe_ts_decaps_from_upipe(upipe);
    upipe_ts_decaps_init_urefcount(upipe);
    upipe_ts_decaps_init_output(upipe);
    upipe_ts_decaps->last_cc = -1;
    upipe_ts_decaps->last_uref = NULL;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This parses and removes the TS header of a packet.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_decaps_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_ts_decaps *upipe_ts_decaps = upipe_ts_decaps_from_upipe(upipe);
    uint8_t buffer[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, TS_HEADER_SIZE,
                                               buffer);
    if (unlikely(ts_header == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }
    bool transporterror = ts_get_transporterror(ts_header);
    bool unitstart = ts_get_unitstart(ts_header);
    uint8_t cc = ts_get_cc(ts_header);
    bool has_payload = ts_has_payload(ts_header);
    bool has_adaptation = ts_has_adaptation(ts_header);
    UBASE_FATAL(upipe, uref_block_peek_unmap(uref, 0, buffer, ts_header))
    UBASE_FATAL(upipe, uref_block_resize(uref, TS_HEADER_SIZE, -1))

    bool discontinuity = upipe_ts_decaps->last_cc == -1;
    if (unlikely(has_adaptation)) {
        uint8_t af_length;
        if (unlikely(!ubase_check(uref_block_extract(uref, 0, 1, &af_length)))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        if (unlikely((!has_payload && af_length != 183) ||
                     (has_payload && af_length >= 183))) {
            upipe_warn(upipe, "invalid adaptation field received");
            /* keep invalid packets with a 0-length payload because
             * it is a common error in the field */
            if (!(has_payload && af_length == 183)) {
                uref_free(uref);
                return;
            }
        }

        if (af_length) {
            uint8_t af_header;
            if (unlikely(!ubase_check(uref_block_extract(uref, 1, 1, &af_header)))) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                return;
            }

            if (unlikely(!discontinuity &&
                     tsaf_has_discontinuity(&af_header - 1 - TS_HEADER_SIZE))) {
                upipe_warn(upipe, "discontinuity flagged");
                discontinuity = true;
            }

            if (tsaf_has_pcr(&af_header - 1 - TS_HEADER_SIZE)) {
                uint8_t buffer2[TS_HEADER_SIZE_PCR - TS_HEADER_SIZE_AF];
                const uint8_t *pcr = uref_block_peek(uref, 2,
                        TS_HEADER_SIZE_PCR - TS_HEADER_SIZE_AF, buffer2);
                if (unlikely(pcr == NULL)) {
                    uref_free(uref);
                    upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
                    return;
                }
                uint64_t pcrval = (tsaf_get_pcr(pcr - TS_HEADER_SIZE_AF) * 300 +
                                   tsaf_get_pcrext(pcr - TS_HEADER_SIZE_AF));
                pcrval *= UCLOCK_FREQ / 27000000;
                UBASE_FATAL(upipe, uref_block_peek_unmap(uref, 2, buffer2, pcr))

                uref_clock_set_ref(uref);
                upipe_throw_clock_ref(upipe, uref, pcrval,
                                      discontinuity ? 1 : 0);
            }
        }

        UBASE_FATAL(upipe, uref_block_resize(uref, af_length + 1, -1))
    }

    if (unlikely(ts_check_duplicate(cc, upipe_ts_decaps->last_cc))) {
        if (!has_payload) {
            /* padding or just PCR */
            uref_free(uref);
            return;
        }
        if (upipe_ts_decaps->last_uref != NULL &&
            ubase_check(uref_block_compare(uref, 0,
                                           upipe_ts_decaps->last_uref))) {
            upipe_dbg(upipe, "removing duplicate packet");
            uref_free(uref);
            return;
        }
        upipe_warn_va(upipe, "potentially lost 16 packets");
        discontinuity = true;
    }

    if (unlikely(!discontinuity &&
                 ts_check_discontinuity(cc, upipe_ts_decaps->last_cc))) {
        upipe_warn_va(upipe, "potentially lost %d packets",
                      (0x10 + cc - upipe_ts_decaps->last_cc - 1) & 0xf);
        discontinuity = true;
    }
    upipe_ts_decaps->last_cc = cc;

    if (unlikely(!has_payload)) {
        uref_free(uref);
        return;
    }

    if (unlikely(discontinuity))
        uref_flow_set_discontinuity(uref);
    if (unlikely(unitstart))
        uref_block_set_start(uref);
    if (unlikely(transporterror))
        uref_flow_set_error(uref);

    uref_free(upipe_ts_decaps->last_uref);
    upipe_ts_decaps->last_uref = uref_dup(uref);
    upipe_ts_decaps_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_decaps_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def_dup, "block.%s",
                                       def + strlen(EXPECTED_FLOW_DEF)))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_ts_decaps_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}


/** @internal @This processes control commands on a ts decaps pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_decaps_control(struct upipe *upipe,
                                   int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_decaps_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_ts_decaps_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_decaps_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_decaps_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_decaps_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_decaps_set_output(upipe, output);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_decaps_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_decaps *upipe_ts_decaps = upipe_ts_decaps_from_upipe(upipe);
    uref_free(upipe_ts_decaps->last_uref);
    upipe_ts_decaps_clean_output(upipe);
    upipe_ts_decaps_clean_urefcount(upipe);
    upipe_ts_decaps_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_decaps_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_DECAPS_SIGNATURE,

    .upipe_alloc = upipe_ts_decaps_alloc,
    .upipe_input = upipe_ts_decaps_input,
    .upipe_control = upipe_ts_decaps_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_decaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_decaps_mgr_alloc(void)
{
    return &upipe_ts_decaps_mgr;
}

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
#include <upipe/upipe_helper_flow.h>
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
    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** last continuity counter for this PID, or -1 */
    int8_t last_cc;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_decaps, upipe, UPIPE_TS_DECAPS_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_decaps, EXPECTED_FLOW_DEF)

UPIPE_HELPER_OUTPUT(upipe_ts_decaps, output, flow_def, flow_def_sent)

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
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_decaps_alloc_flow(mgr, uprobe, signature,
                                                     args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_decaps *upipe_ts_decaps = upipe_ts_decaps_from_upipe(upipe);
    upipe_ts_decaps_init_output(upipe);
    upipe_ts_decaps->last_cc = -1;
    upipe_throw_ready(upipe);

    const char *def;
    if (unlikely(!uref_flow_get_def(flow_def, &def) ||
                 !uref_flow_set_def_va(flow_def, "block.%s",
                                       def + strlen(EXPECTED_FLOW_DEF))))
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
    upipe_ts_decaps_store_flow_def(upipe, flow_def);
    return upipe;
}

/** @internal @This parses and removes the TS header of a packet.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_decaps_work(struct upipe *upipe, struct uref *uref,
                                 struct upump *upump)
{
    struct upipe_ts_decaps *upipe_ts_decaps = upipe_ts_decaps_from_upipe(upipe);
    bool ret;
    uint8_t buffer[TS_HEADER_SIZE];
    const uint8_t *ts_header = uref_block_peek(uref, 0, TS_HEADER_SIZE,
                                               buffer);
    if (unlikely(ts_header == NULL)) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }
    bool transporterror = ts_get_transporterror(ts_header);
    bool unitstart = ts_get_unitstart(ts_header);
    uint8_t cc = ts_get_cc(ts_header);
    bool has_payload = ts_has_payload(ts_header);
    bool has_adaptation = ts_has_adaptation(ts_header);
    ret = uref_block_peek_unmap(uref, 0, buffer, ts_header);
    assert(ret);
    ret = uref_block_resize(uref, TS_HEADER_SIZE, -1);
    assert(ret);

    bool discontinuity = upipe_ts_decaps->last_cc == -1;
    if (unlikely(has_adaptation)) {
        uint8_t af_length;
        if (unlikely(!uref_block_extract(uref, 0, 1, &af_length))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        if (unlikely((!has_payload && af_length != 183) ||
                     (has_payload && af_length >= 183))) {
            upipe_warn(upipe, "invalid adaptation field received");
            uref_free(uref);
            return;
        }

        if (af_length) {
            uint8_t af_header;
            if (unlikely(!uref_block_extract(uref, 1, 1, &af_header))) {
                uref_free(uref);
                upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
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
                    upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
                    return;
                }
                uint64_t pcrval = (tsaf_get_pcr(pcr - TS_HEADER_SIZE_AF) * 300 +
                                   tsaf_get_pcrext(pcr - TS_HEADER_SIZE_AF));
                pcrval *= UCLOCK_FREQ / 27000000;
                ret = uref_block_peek_unmap(uref, 2, buffer2, pcr);
                assert(ret);

                uref_clock_set_ref(uref);
                upipe_throw_clock_ref(upipe, uref, pcrval,
                                      discontinuity ? 1 : 0);
            }
        }

        ret = uref_block_resize(uref, af_length + 1, -1);
        assert(ret);
    }

    if (unlikely(ts_check_duplicate(cc, upipe_ts_decaps->last_cc))) {
        uref_free(uref);
        return;
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
    if (unlikely((transporterror && !uref_flow_set_error(uref)))) {
        uref_free(uref);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    upipe_ts_decaps_output(upipe, uref, upump);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_decaps_input(struct upipe *upipe, struct uref *uref,
                                  struct upump *upump)
{
    if (unlikely(uref->ubuf == NULL)) {
        upipe_ts_decaps_output(upipe, uref, upump);
        return;
    }

    upipe_ts_decaps_work(upipe, uref, upump);
}

/** @internal @This processes control commands on a ts decaps pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_decaps_control(struct upipe *upipe,
                                    enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_decaps_get_flow_def(upipe, p);
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
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_decaps_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_decaps_clean_output(upipe);
    upipe_ts_decaps_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_decaps_mgr = {
    .signature = UPIPE_TS_DECAPS_SIGNATURE,

    .upipe_alloc = upipe_ts_decaps_alloc,
    .upipe_input = upipe_ts_decaps_input,
    .upipe_control = upipe_ts_decaps_control,
    .upipe_free = upipe_ts_decaps_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_decaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_decaps_mgr_alloc(void)
{
    return &upipe_ts_decaps_mgr;
}

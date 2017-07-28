/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short Upipe module calculating the T-STD buffering latency
 */

#include <upipe/ubase.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_tstd.h>
#include <upipe-ts/upipe_ts_mux.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

/** upipe_ts_tstd structure */ 
struct upipe_ts_tstd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** output pipe */
    struct upipe *output;
    /** flow_definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** maximum retention delay */
    uint64_t max_delay;
    /** buffer size, in octets */
    uint64_t bs;
    /** octetrate of incoming flow */
    uint64_t octetrate;

    /** remainder of the octetrate / fps division */
    long long remainder;
    /** portion of the buffer that has not been distributed to frames, in
     * octets */
    int64_t fullness;
    /** previous DTS */
    uint64_t last_dts;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_tstd, upipe, UPIPE_TS_TSTD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_tstd, urefcount, upipe_ts_tstd_free)
UPIPE_HELPER_VOID(upipe_ts_tstd)
UPIPE_HELPER_OUTPUT(upipe_ts_tstd, output, flow_def, output_state, request_list)

/** @internal @This handles urefs.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_tstd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_tstd *upipe_ts_tstd = upipe_ts_tstd_from_upipe(upipe);
    uint64_t dts;
    if (ubase_check(uref_clock_get_dts_prog(uref, &dts))) {
        if (upipe_ts_tstd->last_dts != UINT64_MAX) {
            uint64_t duration = dts - upipe_ts_tstd->last_dts;
            lldiv_t q = lldiv(duration * upipe_ts_tstd->octetrate +
                              upipe_ts_tstd->remainder, UCLOCK_FREQ);
            upipe_ts_tstd->fullness += q.quot;
            upipe_ts_tstd->remainder = q.rem;
        }
        upipe_ts_tstd->last_dts = dts;
    }

    size_t uref_size = 0;
    uref_block_size(uref, &uref_size);
    upipe_ts_tstd->fullness -= uref_size;
    if (upipe_ts_tstd->fullness < 0) {
        upipe_warn_va(upipe, "T-STD underflow (%"PRId64" octets)",
                      -upipe_ts_tstd->fullness);
        upipe_ts_tstd->fullness = 0;
    } else if (upipe_ts_tstd->fullness > upipe_ts_tstd->bs) {
        upipe_verbose_va(upipe, "T-STD overflow (%"PRId64" octets)",
                         upipe_ts_tstd->fullness - upipe_ts_tstd->bs);
        upipe_ts_tstd->fullness = upipe_ts_tstd->bs;
    }

    uint64_t delay = (upipe_ts_tstd->fullness * UCLOCK_FREQ) /
                     upipe_ts_tstd->octetrate;
    uref_clock_set_cr_dts_delay(uref, delay);
    uref_clock_rebase_cr_prog(uref);
    uref_clock_rebase_cr_orig(uref);

    uint64_t dts_sys;
    struct urational drift_rate;
    if (ubase_check(uref_clock_get_dts_sys(uref, &dts_sys)) &&
        ubase_check(uref_clock_get_rate(uref, &drift_rate)))
        uref_clock_set_cr_sys(uref,
                dts_sys - delay * drift_rate.num / drift_rate.den);
    else
        uref_clock_rebase_cr_sys(uref);

    upipe_ts_tstd_output(upipe, uref, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_tstd_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct upipe_ts_tstd *upipe_ts_tstd = upipe_ts_tstd_from_upipe(upipe);
    UBASE_RETURN(uref_block_flow_get_octetrate(flow_def,
                                               &upipe_ts_tstd->octetrate))
    uint64_t bs;
    UBASE_RETURN(uref_block_flow_get_buffer_size(flow_def, &bs))

    if (bs * UCLOCK_FREQ / upipe_ts_tstd->octetrate >
            upipe_ts_tstd->max_delay) {
        bs = upipe_ts_tstd->max_delay * upipe_ts_tstd->octetrate / UCLOCK_FREQ;
        upipe_warn_va(upipe,
                "exceeding max retention delay, adjusting buffer to %"PRIu64,
                bs);
    }
    upipe_ts_tstd->fullness += bs - upipe_ts_tstd->bs;
    upipe_ts_tstd->bs = bs;
    upipe_ts_tstd->remainder = 0;

    uint64_t latency = 0;
    uref_clock_get_latency(flow_def, &latency);
    latency += bs * UCLOCK_FREQ / upipe_ts_tstd->octetrate;

    struct uref *flow_def_dup;
    if ((flow_def_dup = uref_dup(flow_def)) == NULL)
        return UBASE_ERR_ALLOC;
    if (latency) {
        UBASE_RETURN(uref_clock_set_latency(flow_def_dup, latency))
    }
    upipe_ts_tstd_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay_p filled in with the delay
 * @return an error code
 */
static int upipe_ts_tstd_get_max_delay(struct upipe *upipe, uint64_t *delay_p)
{
    struct upipe_ts_tstd *upipe_ts_tstd = upipe_ts_tstd_from_upipe(upipe);
    assert(delay_p != NULL);
    *delay_p = upipe_ts_tstd->max_delay;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the maximum retention delay.
 *
 * @param upipe description structure of the pipe
 * @param delay new delay
 * @return an error code
 */
static int upipe_ts_tstd_set_max_delay(struct upipe *upipe, uint64_t delay)
{
    struct upipe_ts_tstd *upipe_ts_tstd = upipe_ts_tstd_from_upipe(upipe);
    upipe_ts_tstd->max_delay = delay;
    if (upipe_ts_tstd->flow_def != NULL)
        return upipe_ts_tstd_set_flow_def(upipe, upipe_ts_tstd->flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_tstd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_tstd_control_output(upipe, command, args));
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_tstd_set_flow_def(upipe, flow_def);
        }

        case UPIPE_TS_MUX_GET_MAX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *delay_p = va_arg(args, uint64_t *);
            return upipe_ts_tstd_get_max_delay(upipe, delay_p);
        }
        case UPIPE_TS_MUX_SET_MAX_DELAY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t delay = va_arg(args, uint64_t);
            return upipe_ts_tstd_set_max_delay(upipe, delay);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This allocates a pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_tstd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_tstd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    upipe_ts_tstd_init_urefcount(upipe);
    upipe_ts_tstd_init_output(upipe);
    struct upipe_ts_tstd *upipe_ts_tstd = upipe_ts_tstd_from_upipe(upipe);
    upipe_ts_tstd->max_delay = UINT64_MAX;
    upipe_ts_tstd->bs = upipe_ts_tstd->fullness = 0;
    upipe_ts_tstd->last_dts = UINT64_MAX;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_tstd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_ts_tstd_clean_output(upipe);
    upipe_ts_tstd_clean_urefcount(upipe);
    upipe_ts_tstd_free_void(upipe);
}

static struct upipe_mgr upipe_ts_tstd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_TSTD_SIGNATURE,

    .upipe_alloc = upipe_ts_tstd_alloc,
    .upipe_input = upipe_ts_tstd_input,
    .upipe_control = upipe_ts_tstd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for probe pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_tstd_mgr_alloc(void)
{
    return &upipe_ts_tstd_mgr;
}

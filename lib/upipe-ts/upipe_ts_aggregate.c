/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe module to aggregate complete TS packets up to specified MTU
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_aggregate.h>
#include <upipe-ts/upipe_ts_mux.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegts."
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define UINT33_MAX UINT64_C(8589934592)
/** default MTU */
#define DEFAULT_MTU (7 * TS_SIZE)

/** @internal @This is the private context of a ts_aggregate pipe. */
struct upipe_ts_agg {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** latency in the input flow */
    uint64_t input_latency;

    /** mux octetrate */
    uint64_t octetrate;
    /** interval between packets (rounded up, not to be used anywhere
     * critical */
    uint64_t interval;
    /** mux mode */
    enum upipe_ts_mux_mode mode;
    /** MTU */
    size_t mtu;

    /** one TS packet of padding */
    struct ubuf *padding;
    /** number of packets dropped since last muxing */
    unsigned int dropped;

    /** date of the next uref (system time) */
    uint64_t next_cr_sys;
    /** date of the previous uref (system time) */
    uint64_t last_cr_sys;
    /** remainder of the uref_size / octetrate calculation */
    uint64_t next_cr_remainder;
    /** next segmented aggregation */
    struct uchain next_urefs;
    /** next urefs size */
    size_t next_urefs_size;
    /** latest departure time of the next_urefs */
    uint64_t next_urefs_dts;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_agg, upipe, UPIPE_TS_AGG_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_agg, urefcount, upipe_ts_agg_free)
UPIPE_HELPER_VOID(upipe_ts_agg)
UPIPE_HELPER_UREF_MGR(upipe_ts_agg, uref_mgr)
UPIPE_HELPER_UBUF_MGR(upipe_ts_agg, ubuf_mgr, flow_def)
UPIPE_HELPER_OUTPUT(upipe_ts_agg, output, flow_def, flow_def_sent)

/** @internal @This allocates a ts_aggregate pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_agg_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_agg_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    upipe_ts_agg_init_urefcount(upipe);
    upipe_ts_agg_init_uref_mgr(upipe);
    upipe_ts_agg_init_ubuf_mgr(upipe);
    upipe_ts_agg_init_output(upipe);
    upipe_ts_agg->input_latency = 0;
    upipe_ts_agg->octetrate = 0;
    upipe_ts_agg->interval = 0;
    upipe_ts_agg->mode = UPIPE_TS_MUX_MODE_VBR;
    upipe_ts_agg->mtu = DEFAULT_MTU;
    upipe_ts_agg->padding = NULL;
    upipe_ts_agg->dropped = 0;
    upipe_ts_agg->next_cr_sys = UINT64_MAX;
    upipe_ts_agg->last_cr_sys = UINT64_MAX;
    upipe_ts_agg->next_cr_remainder = 0;
    ulist_init(&upipe_ts_agg->next_urefs);
    upipe_ts_agg->next_urefs_size = 0;
    upipe_ts_agg->next_urefs_dts = UINT64_MAX;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This initializes the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_agg_init(struct upipe *upipe)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(!ubase_check(upipe_ts_agg_check_ubuf_mgr(upipe))))
        return;

    upipe_ts_agg->padding = ubuf_block_alloc(upipe_ts_agg->ubuf_mgr, TS_SIZE);
    if (unlikely(upipe_ts_agg->padding == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubase_check(ubuf_block_write(upipe_ts_agg->padding, 0, &size,
                                               &buffer)))) {
        ubuf_free(upipe_ts_agg->padding);
        upipe_ts_agg->padding = NULL;
        return;
    }
    assert(size == TS_SIZE);
    ts_pad(buffer);
    ubuf_block_unmap(upipe_ts_agg->padding, 0);
}

/** @internal In capped VBR mode, @this checks if the next uref can be skipped
 * by one or several ticks, and changes the clock references accordingly.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys clock reference to which we'd like to move the next uref
 */
static bool upipe_ts_agg_try_shift(struct upipe *upipe, uint64_t cr_sys)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    uint64_t next_cr_sys = upipe_ts_agg->next_cr_sys;
    uint64_t next_cr_remainder = upipe_ts_agg->next_cr_remainder;

    while (cr_sys > next_cr_sys + upipe_ts_agg->interval) {
        lldiv_t q = lldiv((uint64_t)upipe_ts_agg->mtu * UCLOCK_FREQ +
                          next_cr_remainder, upipe_ts_agg->octetrate);
        next_cr_sys += q.quot;
        next_cr_remainder = q.rem;
    }

    if (next_cr_sys > upipe_ts_agg->next_urefs_dts)
        return false;
    upipe_ts_agg->next_cr_sys = next_cr_sys;
    upipe_ts_agg->next_cr_remainder = next_cr_remainder;
    return true;
}

/** @internal @This rewrites the PCR according to the new output date.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_agg_fix_pcr(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    uint8_t ts_header[TS_HEADER_SIZE_PCR];

    if (unlikely(!ubase_check(uref_block_extract(uref, 0, TS_HEADER_SIZE_PCR,
                                                 ts_header)))) {
        uref_free(uref);
        upipe_warn_va(upipe, "couldn't read TS header from aggregate");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        return;
    }

    if (ts_has_adaptation(ts_header) && ts_get_adaptation(ts_header) &&
        tsaf_has_pcr(ts_header)) {
        /* We suppose the header was allocated by ts_encaps, so in a
         * single piece, and we can do the following. */
        uint8_t *buffer;
        int size = TS_HEADER_SIZE_PCR;
        uint64_t orig_cr_sys, orig_cr_prog;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref, &orig_cr_sys)) ||
                     !ubase_check(uref_clock_get_cr_prog(uref, &orig_cr_prog)) ||
                     !ubase_check(uref_block_write(uref, 0, &size, &buffer))))
            upipe_warn_va(upipe, "couldn't fix PCR");
        else {
            orig_cr_prog += upipe_ts_agg->next_cr_sys - orig_cr_sys;
            tsaf_set_pcr(buffer, (orig_cr_prog / 300) % UINT33_MAX);
            tsaf_set_pcrext(buffer, orig_cr_prog % 300);
            uref_block_unmap(uref, 0);
        }
    }
}

/** @internal @This outputs a buffer of mtu % TS_SIZE, using padding if
 * necessary, and rewrites PCR if necessary.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_agg_complete(struct upipe *upipe, struct upump **upump_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);

    if (upipe_ts_agg->next_cr_sys != UINT64_MAX)
        upipe_ts_agg->last_cr_sys = upipe_ts_agg->next_cr_sys;

    struct uchain *uchain;
    ulist_foreach (&upipe_ts_agg->next_urefs, uchain) {
        struct uref *uref = uref_from_uchain(uchain);
        if (ubase_check(uref_clock_get_ref(uref))) {
            /* fix the PCR according to the new output date */
            upipe_ts_agg_fix_pcr(upipe, uref);
        }
    }

    uint64_t next_cr_sys = upipe_ts_agg->next_cr_sys;
    if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR) {
        lldiv_t q = lldiv((uint64_t)upipe_ts_agg->mtu * UCLOCK_FREQ +
                          upipe_ts_agg->next_cr_remainder,
                          upipe_ts_agg->octetrate);
        upipe_ts_agg->next_cr_sys += q.quot;
        upipe_ts_agg->next_cr_remainder = q.rem;
    } else {
        upipe_ts_agg->next_cr_sys = UINT64_MAX;
    }

    uchain = ulist_pop(&upipe_ts_agg->next_urefs);
    struct uref *uref = uref_from_uchain(uchain);
    if (uref == NULL) {
        if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_CBR)
            /* Do not output a packet. */
            return;

        if (unlikely(!ubase_check(upipe_ts_agg_check_uref_mgr(upipe))))
            return;

        uref = uref_block_alloc(upipe_ts_agg->uref_mgr, upipe_ts_agg->ubuf_mgr,
                                0);
    }
    uref_clock_set_cr_sys(uref, next_cr_sys);
    /* DVB-IPI does not require the RTP clock to be sync'ed to cr_prog, so
     * sync it agains cr_sys. */
    uref_clock_delete_date_prog(uref);

    struct uchain *uchain_tmp;
    ulist_delete_foreach (&upipe_ts_agg->next_urefs, uchain, uchain_tmp) {
        struct uref *uref_append = uref_from_uchain(uchain);
        ulist_delete(uchain);
        struct ubuf *append = uref_detach_ubuf(uref_append);
        uref_free(uref_append);
        if (unlikely(!ubase_check(uref_block_append(uref, append)))) {
            upipe_warn(upipe, "error appending packet");
            ubuf_free(append);
        }
    }

    unsigned int padding = 0;
    while (upipe_ts_agg->next_urefs_size + TS_SIZE <= upipe_ts_agg->mtu) {
        struct ubuf *ubuf = ubuf_dup(upipe_ts_agg->padding);
        if (unlikely(ubuf == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }

        uref_block_append(uref, ubuf);
        upipe_ts_agg->next_urefs_size += TS_SIZE;
        padding++;
    }
    if (padding)
        upipe_verbose_va(upipe, "inserting %u padding at %"PRIu64, padding,
                         next_cr_sys);

    upipe_ts_agg->next_urefs_size = 0;
    upipe_ts_agg->next_urefs_dts = UINT64_MAX;

    upipe_ts_agg_output(upipe, uref, upump_p);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_agg_input(struct upipe *upipe, struct uref *uref,
                               struct upump **upump_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);

    if (unlikely(upipe_ts_agg->padding == NULL))
        upipe_ts_agg_init(upipe);
    if (unlikely(upipe_ts_agg->padding == NULL)) {
        uref_free(uref);
        return;
    }

    if (unlikely(upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR &&
                 upipe_ts_agg->octetrate == 0)) {
        uref_free(uref);
        upipe_warn(upipe, "invalid mux octetrate");
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        return;
    }

    size_t size = 0;
    const size_t mtu = upipe_ts_agg->mtu;

    uref_block_size(uref, &size);

    /* check for invalid size */
    if (unlikely(size != TS_SIZE)) {
        upipe_warn_va(upipe,
            "received packet of invalid size: %zu (mtu == %zu)", size, mtu);
        uref_free(uref);
        return;
    }

    uint64_t dts_sys = UINT64_MAX;
    if (unlikely(!ubase_check(uref_clock_get_dts_sys(uref, &dts_sys)) &&
                 upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR)) {
        upipe_warn(upipe, "non-dated packet received");
        uref_free(uref);
        return;
    }
    uint64_t delay = 0;
    uref_clock_get_cr_dts_delay(uref, &delay);

    if (upipe_ts_agg->next_cr_sys == UINT64_MAX && dts_sys != UINT64_MAX)
        upipe_ts_agg->next_cr_sys = dts_sys - delay;

    /* packet in the past */
    if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR &&
        upipe_ts_agg->next_cr_sys > dts_sys + upipe_ts_agg->interval) {
        upipe_verbose_va(upipe, "dropping late packet %"PRIu64" > %"PRIu64,
                         upipe_ts_agg->next_cr_sys,
                         dts_sys + upipe_ts_agg->interval);
        uref_free(uref);
        upipe_ts_agg->dropped++;
        return;
    }

    if (upipe_ts_agg->dropped) {
        upipe_warn_va(upipe, "%u packets dropped", upipe_ts_agg->dropped);
        upipe_ts_agg->dropped = 0;
    }

    /* packet in the future that would arrive too early if muxed into this
     * aggregate */
    if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR &&
        dts_sys - delay > upipe_ts_agg->next_cr_sys + upipe_ts_agg->interval) {
        if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_CAPPED ||
            !upipe_ts_agg_try_shift(upipe, dts_sys - delay))
            upipe_ts_agg_complete(upipe, upump_p);
    }

    if (dts_sys < upipe_ts_agg->next_urefs_dts)
        upipe_ts_agg->next_urefs_dts = dts_sys;
    ulist_add(&upipe_ts_agg->next_urefs, uref_to_uchain(uref));

    /* anticipate next packet size and flush now if necessary */
    upipe_ts_agg->next_urefs_size += size;
    if (upipe_ts_agg->next_urefs_size + TS_SIZE > mtu)
        upipe_ts_agg_complete(upipe, upump_p);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_agg_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL))
        return UBASE_ERR_ALLOC;

    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    upipe_ts_agg->input_latency = 0;
    uref_clock_get_latency(flow_def, &upipe_ts_agg->input_latency);

    if (unlikely(!ubase_check(uref_flow_set_def(flow_def_dup,
                                                "block.mpegtsaligned.")) ||
                 !ubase_check(uref_clock_set_latency(flow_def_dup,
                                upipe_ts_agg->input_latency +
                                upipe_ts_agg->interval)) ||
                 !ubase_check(uref_block_flow_set_octetrate(flow_def_dup,
                                upipe_ts_agg->octetrate))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_ts_agg_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return an error code
 */
static int upipe_ts_agg_get_octetrate(struct upipe *upipe,
                                      uint64_t *octetrate_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(octetrate_p != NULL);
    *octetrate_p = upipe_ts_agg->octetrate;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return an error code
 */
static int upipe_ts_agg_set_octetrate(struct upipe *upipe, uint64_t octetrate)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(octetrate != 0);
    if (upipe_ts_agg->octetrate != octetrate)
        upipe_ts_agg->next_cr_remainder = 0;
    upipe_ts_agg->octetrate = octetrate;
    upipe_ts_agg->interval = upipe_ts_agg->mtu * UCLOCK_FREQ /
                             upipe_ts_agg->octetrate;
    upipe_notice_va(upipe, "now operating in %s mode at %"PRIu64" bits/s",
                    upipe_ts_agg->mode == UPIPE_TS_MUX_MODE_VBR ? "VBR" :
                    upipe_ts_agg->mode == UPIPE_TS_MUX_MODE_CBR ? "CBR" :
                    "capped VBR", octetrate * 8);

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(upipe_ts_agg->flow_def)) == NULL ||
                 !ubase_check(uref_clock_set_latency(flow_def_dup,
                                upipe_ts_agg->input_latency +
                                upipe_ts_agg->interval)) ||
                 !ubase_check(uref_block_flow_set_octetrate(flow_def_dup,
                                upipe_ts_agg->octetrate))))
        return UBASE_ERR_ALLOC;
    upipe_ts_agg_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the current mode.
 *
 * @param upipe description structure of the pipe
 * @param mode_p filled in with the mode
 * @return an error code
 */
static int upipe_ts_agg_get_mode(struct upipe *upipe,
                                 enum upipe_ts_mux_mode *mode_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(mode_p != NULL);
    *mode_p = upipe_ts_agg->mode;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the mode.
 *
 * @param upipe description structure of the pipe
 * @param mode new mode
 * @return an error code
 */
static int upipe_ts_agg_set_mode(struct upipe *upipe,
                                 enum upipe_ts_mux_mode mode)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    upipe_ts_agg->mode = mode;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @return an error code
 */
static int upipe_ts_agg_get_mtu(struct upipe *upipe, unsigned int *mtu_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(mtu_p != NULL);
    *mtu_p = upipe_ts_agg->mtu;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu configured mtu, in octets
 * @return an error code
 */
static int upipe_ts_agg_set_mtu(struct upipe *upipe, unsigned int mtu)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(mtu < TS_SIZE))
        return UBASE_ERR_INVALID;
    mtu -= mtu % TS_SIZE;
    if (mtu < upipe_ts_agg->next_urefs_size + TS_SIZE)
        upipe_ts_agg_complete(upipe, NULL);
    upipe_ts_agg->mtu = mtu;
    if (upipe_ts_agg->octetrate)
        upipe_ts_agg->interval = upipe_ts_agg->mtu * UCLOCK_FREQ /
                                 upipe_ts_agg->octetrate;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_agg_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR:
            return upipe_ts_agg_attach_uref_mgr(upipe);
        case UPIPE_ATTACH_UBUF_MGR:
            return upipe_ts_agg_attach_ubuf_mgr(upipe);

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_agg_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_agg_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_agg_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_agg_set_output(upipe, output);
        }

        case UPIPE_TS_MUX_GET_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *octetrate_p = va_arg(args, uint64_t *);
            return upipe_ts_agg_get_octetrate(upipe, octetrate_p);
        }
        case UPIPE_TS_MUX_SET_OCTETRATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t octetrate = va_arg(args, uint64_t);
            return upipe_ts_agg_set_octetrate(upipe, octetrate);
        }
        case UPIPE_TS_MUX_GET_MODE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            enum upipe_ts_mux_mode *mode_p = va_arg(args,
                                                    enum upipe_ts_mux_mode *);
            return upipe_ts_agg_get_mode(upipe, mode_p);
        }
        case UPIPE_TS_MUX_SET_MODE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            enum upipe_ts_mux_mode mode = va_arg(args, enum upipe_ts_mux_mode);
            return upipe_ts_agg_set_mode(upipe, mode);
        }
        case UPIPE_TS_MUX_GET_MTU: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int *mtu_p = va_arg(args, unsigned int *);
            return upipe_ts_agg_get_mtu(upipe, mtu_p);
        }
        case UPIPE_TS_MUX_SET_MTU: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            unsigned int mtu = va_arg(args, unsigned int);
            return upipe_ts_agg_set_mtu(upipe, mtu);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_agg_free(struct upipe *upipe)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(!ulist_empty(&upipe_ts_agg->next_urefs)))
        upipe_ts_agg_complete(upipe, NULL);

    upipe_throw_dead(upipe);
    if (likely(upipe_ts_agg->padding))
        ubuf_free(upipe_ts_agg->padding);
    upipe_ts_agg_clean_output(upipe);
    upipe_ts_agg_clean_ubuf_mgr(upipe);
    upipe_ts_agg_clean_uref_mgr(upipe);
    upipe_ts_agg_clean_urefcount(upipe);
    upipe_ts_agg_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_agg_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_AGG_SIGNATURE,

    .upipe_alloc = upipe_ts_agg_alloc,
    .upipe_input = upipe_ts_agg_input,
    .upipe_control = upipe_ts_agg_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_aggregate pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_agg_mgr_alloc(void)
{
    return &upipe_ts_agg_mgr;
}

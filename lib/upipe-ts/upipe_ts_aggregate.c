/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow.h>
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

    /** DTS of the next uref */
    uint64_t next_dts;
    /** DTS of the next uref (system time) */
    uint64_t next_dts_sys;
    /** remainder of the uref_size / octetrate calculation */
    uint64_t next_dts_remainder;
    /** next segmented aggregation */
    struct uref *next_uref;
    /** next uref size */
    size_t next_uref_size;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_agg, upipe, UPIPE_TS_AGG_SIGNATURE)
UPIPE_HELPER_FLOW(upipe_ts_agg, EXPECTED_FLOW_DEF)
UPIPE_HELPER_UREF_MGR(upipe_ts_agg, uref_mgr)
UPIPE_HELPER_UBUF_MGR(upipe_ts_agg, ubuf_mgr)
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
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_agg_alloc_flow(mgr, uprobe, signature,
                                                  args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    if (unlikely(!uref_flow_set_def(flow_def, "block.mpegtsaligned."))) {
        uref_free(flow_def);
        upipe_ts_agg_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    upipe_ts_agg_init_uref_mgr(upipe);
    upipe_ts_agg_init_ubuf_mgr(upipe);
    upipe_ts_agg_init_output(upipe);
    upipe_ts_agg->octetrate = 0;
    upipe_ts_agg->interval = UINT64_MAX;
    upipe_ts_agg->mode = UPIPE_TS_MUX_MODE_VBR;
    upipe_ts_agg->mtu = DEFAULT_MTU;
    upipe_ts_agg->padding = NULL;
    upipe_ts_agg->dropped = 0;
    upipe_ts_agg->next_dts = UINT64_MAX;
    upipe_ts_agg->next_dts_sys = UINT64_MAX;
    upipe_ts_agg->next_dts_remainder = 0;
    upipe_ts_agg->next_uref = NULL;
    upipe_ts_agg->next_uref_size = 0;
    upipe_ts_agg_store_flow_def(upipe, flow_def);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This initializes the pipe;
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_agg_init(struct upipe *upipe)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(upipe_ts_agg->ubuf_mgr == NULL))
        upipe_throw_need_ubuf_mgr(upipe, upipe_ts_agg->flow_def);
    if (unlikely(upipe_ts_agg->ubuf_mgr == NULL))
        return;

    upipe_ts_agg->padding = ubuf_block_alloc(upipe_ts_agg->ubuf_mgr, TS_SIZE);
    if (unlikely(upipe_ts_agg->padding == NULL)) {
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubuf_block_write(upipe_ts_agg->padding, 0, &size, &buffer))) {
        ubuf_free(upipe_ts_agg->padding);
        upipe_ts_agg->padding = NULL;
        return;
    }
    assert(size == TS_SIZE);
    ts_pad(buffer);
    ubuf_block_unmap(upipe_ts_agg->padding, 0);
}

/** @internal @This outputs a buffer of mtu % TS_SIZE, using padding if
 * necessary, and rewrites PCR if necessary.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_agg_complete(struct upipe *upipe, struct upump *upump)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    struct uref *uref = upipe_ts_agg->next_uref;
    size_t uref_size = upipe_ts_agg->next_uref_size;
    uint64_t next_dts = upipe_ts_agg->next_dts;
    uint64_t next_dts_sys = upipe_ts_agg->next_dts_sys;

    if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR) {
        lldiv_t q = lldiv((uint64_t)upipe_ts_agg->mtu * UCLOCK_FREQ +
                          upipe_ts_agg->next_dts_remainder,
                          upipe_ts_agg->octetrate);
        upipe_ts_agg->next_dts += q.quot;
        if (upipe_ts_agg->next_dts_sys != UINT64_MAX)
            upipe_ts_agg->next_dts_sys += q.quot;
        upipe_ts_agg->next_dts_remainder = q.rem;
    } else {
        upipe_ts_agg->next_dts = UINT64_MAX;
        upipe_ts_agg->next_dts_sys = UINT64_MAX;
    }
    upipe_ts_agg->next_uref = NULL;
    upipe_ts_agg->next_uref_size = 0;

    if (uref == NULL) {
        if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_CBR)
            /* Do not output a packet. */
            return;

        if (unlikely(upipe_ts_agg->uref_mgr == NULL))
            upipe_throw_need_uref_mgr(upipe);
        if (unlikely(upipe_ts_agg->uref_mgr == NULL))
            return;

        uref = uref_block_alloc(upipe_ts_agg->uref_mgr, upipe_ts_agg->ubuf_mgr,
                                0);
        uref_clock_set_dts(uref, next_dts);
        if (next_dts_sys != UINT64_MAX) {
            uref_clock_set_dts_sys(uref, next_dts_sys);
            uref_clock_set_systime(uref, next_dts_sys);
        }
    }

    while (uref_size + TS_SIZE <= upipe_ts_agg->mtu) {
        struct ubuf *ubuf = ubuf_dup(upipe_ts_agg->padding);
        if (unlikely(ubuf == NULL)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }

        uref_block_append(uref, ubuf);
        uref_size += TS_SIZE;
    }

    int offset = 0;
    while (offset + TS_SIZE <= upipe_ts_agg->mtu) {
        uint8_t ts_header[TS_HEADER_SIZE_PCR];
        if (unlikely(!uref_block_extract(uref, offset, TS_HEADER_SIZE_PCR,
                                         ts_header))) {
            uref_free(uref);
            upipe_warn_va(upipe, "couldn't read TS header from aggregate");
            upipe_throw_error(upipe, UPROBE_ERR_INVALID);
            return;
        }

        if (ts_has_adaptation(ts_header) && ts_get_adaptation(ts_header) &&
            tsaf_has_pcr(ts_header)) {
            /* We suppose the header was allocated by ts_encaps, so in a
             * single piece, and we can do the following. */
            uint8_t *buffer;
            int size = TS_HEADER_SIZE_PCR;
            if (unlikely(!uref_block_write(uref, offset, &size, &buffer)))
                upipe_warn_va(upipe, "couldn't fix PCR");
            else {
                tsaf_set_pcr(buffer, (next_dts / 300) % UINT33_MAX);
                tsaf_set_pcrext(buffer, next_dts % 300);
                uref_block_unmap(uref, offset);
            }
        }

        offset += TS_SIZE;
    }

    upipe_ts_agg_output(upipe, uref, upump);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_agg_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_ts_agg_output(upipe, uref, upump);
        return;
    }

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
        upipe_throw_error(upipe, UPROBE_ERR_INVALID);
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

    uint64_t dts = UINT64_MAX;
    if (unlikely(!uref_clock_get_dts(uref, &dts) &&
                 upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR)) {
        upipe_warn(upipe, "non-dated packet received");
        uref_free(uref);
        return;
    }
    uint64_t delay = 0;
    uref_clock_get_vbv_delay(uref, &delay);

    if (upipe_ts_agg->next_dts == UINT64_MAX) {
        upipe_ts_agg->next_dts = dts - delay;
        if (uref_clock_get_dts_sys(uref, &upipe_ts_agg->next_dts_sys))
            upipe_ts_agg->next_dts_sys -= delay;
    }

    /* packet in the past */
    if (upipe_ts_agg->mode != UPIPE_TS_MUX_MODE_VBR &&
        dts + upipe_ts_agg->interval < upipe_ts_agg->next_dts) {
        uint8_t ts_header[TS_HEADER_SIZE];
        uref_block_extract(uref, 0, TS_HEADER_SIZE, ts_header);
        upipe_verbose_va(upipe, "dropping late packet %"PRIu16" %"PRIu64,
                         ts_get_pid(ts_header),
                         (upipe_ts_agg->next_dts - dts) / 27);
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
        dts - delay >= upipe_ts_agg->next_dts + upipe_ts_agg->interval)
        upipe_ts_agg_complete(upipe, upump);

    /* keep or attach incoming packet */
    if (unlikely(upipe_ts_agg->next_uref == NULL)) {
        if (upipe_ts_agg->next_dts != UINT64_MAX) {
            if (upipe_ts_agg->next_dts_sys != UINT64_MAX) {
                uref_clock_set_dts_sys(uref, upipe_ts_agg->next_dts_sys);
                uref_clock_set_systime(uref, upipe_ts_agg->next_dts_sys);
            }
            uref_clock_set_dts(uref, upipe_ts_agg->next_dts);
        }
        uref_clock_delete_vbv_delay(uref);

        upipe_ts_agg->next_uref = uref;
        upipe_ts_agg->next_uref_size = size;

    } else {
        struct ubuf *append = uref_detach_ubuf(uref);
        uref_free(uref);
        if (unlikely(!uref_block_append(upipe_ts_agg->next_uref, append))) {
            upipe_warn(upipe, "error appending packet");
            ubuf_free(append);
            return;
        };
        upipe_ts_agg->next_uref_size += size;
    }

    /* anticipate next packet size and flush now if necessary */
    if (upipe_ts_agg->next_uref_size + TS_SIZE > mtu)
        upipe_ts_agg_complete(upipe, upump);
}

/** @internal @This returns the current mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate_p filled in with the octetrate
 * @return false in case of error
 */
static bool upipe_ts_agg_get_octetrate(struct upipe *upipe,
                                       uint64_t *octetrate_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(octetrate_p != NULL);
    *octetrate_p = upipe_ts_agg->octetrate;
    return true;
}

/** @internal @This sets the mux octetrate.
 *
 * @param upipe description structure of the pipe
 * @param octetrate new octetrate
 * @return false in case of error
 */
static bool upipe_ts_agg_set_octetrate(struct upipe *upipe, uint64_t octetrate)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(octetrate != 0);
    if (upipe_ts_agg->octetrate != octetrate)
        upipe_ts_agg->next_dts_remainder = 0;
    upipe_ts_agg->octetrate = octetrate;
    upipe_ts_agg->interval = upipe_ts_agg->mtu * UCLOCK_FREQ /
                             upipe_ts_agg->octetrate;
    upipe_notice_va(upipe, "now operating in %s mode at %"PRIu64" bits/s",
                    upipe_ts_agg->mode == UPIPE_TS_MUX_MODE_VBR ? "VBR" :
                    upipe_ts_agg->mode == UPIPE_TS_MUX_MODE_CBR ? "CBR" :
                    "capped VBR", octetrate * 8);
    return true;
}

/** @internal @This returns the current mode.
 *
 * @param upipe description structure of the pipe
 * @param mode_p filled in with the mode
 * @return false in case of error
 */
static bool upipe_ts_agg_get_mode(struct upipe *upipe,
                                  enum upipe_ts_mux_mode *mode_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(mode_p != NULL);
    *mode_p = upipe_ts_agg->mode;
    return true;
}

/** @internal @This sets the mode.
 *
 * @param upipe description structure of the pipe
 * @param mode new mode
 * @return false in case of error
 */
static bool upipe_ts_agg_set_mode(struct upipe *upipe,
                                  enum upipe_ts_mux_mode mode)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    upipe_ts_agg->mode = mode;
    return true;
}

/** @internal @This returns the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu_p filled in with the configured mtu, in octets
 * @return false in case of error
 */
static bool upipe_ts_agg_get_mtu(struct upipe *upipe, unsigned int *mtu_p)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    assert(mtu_p != NULL);
    *mtu_p = upipe_ts_agg->mtu;
    return true;
}

/** @internal @This sets the configured mtu.
 *
 * @param upipe description structure of the pipe
 * @param mtu configured mtu, in octets
 * @return false in case of error
 */
static bool upipe_ts_agg_set_mtu(struct upipe *upipe, unsigned int mtu)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(mtu < TS_SIZE))
        return false;
    mtu -= mtu % TS_SIZE;
    if (mtu < upipe_ts_agg->next_uref_size + TS_SIZE)
        upipe_ts_agg_complete(upipe, NULL);
    upipe_ts_agg->mtu = mtu;
    if (upipe_ts_agg->octetrate)
        upipe_ts_agg->interval = upipe_ts_agg->mtu * UCLOCK_FREQ /
                                 upipe_ts_agg->octetrate;
    return true;
}

/** @internal @This processes control commands on a ts check pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_agg_control(struct upipe *upipe,
                                 enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_ts_agg_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_ts_agg_set_uref_mgr(upipe, uref_mgr);
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_ts_agg_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_ts_agg_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_agg_get_flow_def(upipe, p);
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
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t *octetrate_p = va_arg(args, uint64_t *);
            return upipe_ts_agg_get_octetrate(upipe, octetrate_p);
        }
        case UPIPE_TS_MUX_SET_OCTETRATE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            uint64_t octetrate = va_arg(args, uint64_t);
            return upipe_ts_agg_set_octetrate(upipe, octetrate);
        }
        case UPIPE_TS_MUX_GET_MODE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            enum upipe_ts_mux_mode *mode_p = va_arg(args,
                                                    enum upipe_ts_mux_mode *);
            return upipe_ts_agg_get_mode(upipe, mode_p);
        }
        case UPIPE_TS_MUX_SET_MODE: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            enum upipe_ts_mux_mode mode = va_arg(args, enum upipe_ts_mux_mode);
            return upipe_ts_agg_set_mode(upipe, mode);
        }
        case UPIPE_TS_MUX_GET_MTU: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            unsigned int *mtu_p = va_arg(args, unsigned int *);
            return upipe_ts_agg_get_mtu(upipe, mtu_p);
        }
        case UPIPE_TS_MUX_SET_MTU: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_MUX_SIGNATURE);
            unsigned int mtu = va_arg(args, unsigned int);
            return upipe_ts_agg_set_mtu(upipe, mtu);
        }
        default:
            return false;
    }
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_agg_free(struct upipe *upipe)
{
    struct upipe_ts_agg *upipe_ts_agg = upipe_ts_agg_from_upipe(upipe);
    if (unlikely(upipe_ts_agg->next_uref != NULL))
        upipe_ts_agg_complete(upipe, NULL);

    upipe_throw_dead(upipe);
    if (likely(upipe_ts_agg->padding))
        ubuf_free(upipe_ts_agg->padding);
    upipe_ts_agg_clean_output(upipe);
    upipe_ts_agg_clean_ubuf_mgr(upipe);
    upipe_ts_agg_clean_uref_mgr(upipe);
    upipe_ts_agg_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_agg_mgr = {
    .signature = UPIPE_TS_AGG_SIGNATURE,

    .upipe_alloc = upipe_ts_agg_alloc,
    .upipe_input = upipe_ts_agg_input,
    .upipe_control = upipe_ts_agg_control,
    .upipe_free = upipe_ts_agg_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_aggregate pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_agg_mgr_alloc(void)
{
    return &upipe_ts_agg_mgr;
}

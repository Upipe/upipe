/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe module encapsulating (adding TS header) PES and PSI access units
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** T-STD standard max retention time - 1 s */
#define T_STD_MAX_RETENTION UCLOCK_FREQ
/** Max hole allowed in CBR/Capped VBR streams */
#define MAX_HOLE UCLOCK_FREQ

/** @hidden */
static bool upipe_ts_encaps_handle(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p);
/** @hidden */
static int upipe_ts_encaps_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_encaps pipe. */
struct upipe_ts_encaps {
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

    /** temporary uref storage (used during urequest) */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers (used during udeal) */
    struct uchain blockers;

    /** PID */
    uint16_t pid;
    /** octetrate */
    uint64_t octetrate;
    /** T-STD TB rate */
    uint64_t tb_rate;
    /** T-STD TS delay (TB buffer) */
    uint64_t ts_delay;
    /** T-STD max retention time */
    uint64_t max_delay;
    /** true if we chop PSI sections */
    bool psi;

    /** PCR interval (or 0) */
    uint64_t pcr_interval;
    /** PCR tolerance */
    uint64_t pcr_tolerance;

    /** last continuity counter for this PID */
    uint8_t last_cc;
    /** muxing date of the next PCR */
    uint64_t next_pcr;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_encaps, upipe, UPIPE_TS_ENCAPS_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_encaps, urefcount, upipe_ts_encaps_free)
UPIPE_HELPER_VOID(upipe_ts_encaps)
UPIPE_HELPER_OUTPUT(upipe_ts_encaps, output, flow_def, output_state, request_list)
UPIPE_HELPER_UREF_MGR(upipe_ts_encaps, uref_mgr, uref_mgr_request,
                      upipe_ts_encaps_check,
                      upipe_ts_encaps_register_output_request,
                      upipe_ts_encaps_unregister_output_request)
UPIPE_HELPER_UBUF_MGR(upipe_ts_encaps, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_encaps_check,
                      upipe_ts_encaps_register_output_request,
                      upipe_ts_encaps_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_ts_encaps, urefs, nb_urefs, max_urefs, blockers, upipe_ts_encaps_handle)

/** @internal @This allocates a ts_encaps pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_encaps_alloc(struct upipe_mgr *mgr,
                                           struct uprobe *uprobe,
                                           uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_encaps_alloc_void(mgr, uprobe, signature,
                                                     args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    upipe_ts_encaps_init_urefcount(upipe);
    upipe_ts_encaps_init_output(upipe);
    upipe_ts_encaps_init_uref_mgr(upipe);
    upipe_ts_encaps_init_ubuf_mgr(upipe);
    upipe_ts_encaps_init_input(upipe);
    upipe_ts_encaps->pid = 8192;
    upipe_ts_encaps->octetrate = 0;
    upipe_ts_encaps->tb_rate = 0;
    upipe_ts_encaps->ts_delay = 0;
    upipe_ts_encaps->max_delay = T_STD_MAX_RETENTION;
    upipe_ts_encaps->psi = false;
    upipe_ts_encaps->pcr_interval = 0;
    upipe_ts_encaps->last_cc = 0;
    upipe_ts_encaps->pcr_tolerance = 0;
    upipe_ts_encaps->next_pcr = UINT64_MAX;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This allocates a TS packet containing padding and a PCR.
 *
 * @param upipe description structure of the pipe
 * @param pcr PCR value to encode
 * @return allocated TS packet
 */
static struct uref *upipe_ts_encaps_pad_pcr(struct upipe *upipe, uint64_t pcr)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    struct uref *output = uref_block_alloc(upipe_ts_encaps->uref_mgr,
                                           upipe_ts_encaps->ubuf_mgr,
                                           TS_SIZE);
    if (unlikely(output == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubase_check(uref_block_write(output, 0, &size, &buffer)))) {
        uref_free(output);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    ts_init(buffer);
    ts_set_pid(buffer, upipe_ts_encaps->pid);
    /* Do not increase continuity counter on packets containing no payload */
    ts_set_cc(buffer, upipe_ts_encaps->last_cc);
    ts_set_adaptation(buffer, TS_SIZE - TS_HEADER_SIZE - 1);
    tsaf_set_pcr(buffer, (pcr / 300) % POW2_33);
    tsaf_set_pcrext(buffer, pcr % 300);

    uref_block_unmap(output, 0);
    return output;
}

/** @internal @This splices a TS packet out of an access unit, and adds
 * the TS header.
 *
 * @param upipe description structure of the pipe
 * @param uref input access unit
 * @param start true if it's the first packet of the access unit
 * @param pcr true if the packet must contain a PCR
 * @param random true if the packet is a random access point
 * @param discontinuity true if the packet must have the discontinuity flag
 * @return allocated TS packet
 */
static struct uref *upipe_ts_encaps_splice(struct upipe *upipe,
                                           struct uref *uref, bool start,
                                           uint64_t pcr, bool random,
                                           bool discontinuity)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    size_t header_size;
    if (unlikely(pcr))
        header_size = TS_HEADER_SIZE_PCR;
    else if (unlikely(discontinuity || random))
        header_size = TS_HEADER_SIZE_AF;
    else
        header_size = TS_HEADER_SIZE;

    size_t uref_size = 0, padding_size = 0;
    uref_block_size(uref, &uref_size);
    if (!uref_size) {
        upipe_dbg(upipe, "splicing an empty uref");
        return NULL;
    }
    if (uref_size < TS_SIZE - header_size) {
        if (upipe_ts_encaps->psi)
            padding_size = TS_SIZE - uref_size - header_size;
        else
            header_size = TS_SIZE - uref_size;
    }

    struct uref *output = uref_block_alloc(upipe_ts_encaps->uref_mgr,
                                           upipe_ts_encaps->ubuf_mgr,
                                           header_size);
    if (unlikely(output == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!ubase_check(uref_block_write(output, 0, &size, &buffer)))) {
        uref_free(output);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    ts_init(buffer);
    ts_set_pid(buffer, upipe_ts_encaps->pid);
    upipe_ts_encaps->last_cc++;
    upipe_ts_encaps->last_cc &= 0xf;
    ts_set_cc(buffer, upipe_ts_encaps->last_cc);
    ts_set_payload(buffer);
    if (start)
        ts_set_unitstart(buffer);

    if (header_size > TS_HEADER_SIZE) {
        ts_set_adaptation(buffer, header_size - TS_HEADER_SIZE - 1);
        if (discontinuity)
            tsaf_set_discontinuity(buffer);
        if (random)
            tsaf_set_randomaccess(buffer);
        if (pcr) {
            tsaf_set_pcr(buffer, (pcr / 300) % POW2_33);
            tsaf_set_pcrext(buffer, pcr % 300);
        }
    }

    uref_block_unmap(output, 0);

    struct ubuf *payload = ubuf_block_splice(uref->ubuf, 0,
                                         TS_SIZE - header_size - padding_size);
    if (unlikely(payload == NULL ||
                 !ubase_check(uref_block_append(output, payload)) ||
                 !ubase_check(uref_block_resize(uref,
                            TS_SIZE - header_size - padding_size, -1)))) {
        if (payload != NULL)
            ubuf_free(payload);
        uref_free(output);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    if (padding_size) {
        /* With PSI, pad with 0xff */
        struct ubuf *padding = ubuf_block_alloc(upipe_ts_encaps->ubuf_mgr,
                                                padding_size);
        size = -1;
        if (unlikely(padding == NULL ||
                     !ubase_check(ubuf_block_write(padding, 0, &size, &buffer)))) {
            uref_free(output);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return NULL;
        }
        memset(buffer, 0xff, size);
        ubuf_block_unmap(padding, 0);

        if (unlikely(!ubase_check(uref_block_append(output, padding)))) {
            ubuf_free(padding);
            uref_free(output);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return NULL;
        }
    }

    return output;
}

/** @internal @This chops the access unit in TS packets and adds TS header.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_encaps_work(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    uint64_t dts_sys, dts_prog = UINT64_MAX, dts_orig = UINT64_MAX, delay = 0;
    if (unlikely(!ubase_check(uref_clock_get_dts_sys(uref, &dts_sys)) ||
                 (!ubase_check(uref_clock_get_dts_prog(uref, &dts_prog)) &&
                  upipe_ts_encaps->next_pcr != UINT64_MAX))) {
        upipe_warn_va(upipe, "non-dated packet received");
        uref_free(uref);
        return;
    }
    uref_clock_get_dts_orig(uref, &dts_orig);
    uref_clock_get_cr_dts_delay(uref, &delay);

    if (delay > upipe_ts_encaps->max_delay)
        /* TODO what do we do then ? */
        upipe_dbg_va(upipe,
                     "input exceeds T-STD max retention time (%"PRIu64" ms)",
                     delay * 1000 / UCLOCK_FREQ);

    size_t size;
    if (!ubase_check(uref_block_size(uref, &size)) || !size) {
        upipe_warn_va(upipe, "empty packet received");
        uref_free(uref);
        return;
    }

    bool start = ubase_check(uref_block_get_start(uref));
    bool discontinuity = ubase_check(uref_flow_get_discontinuity(uref));

    uint64_t duration = (uint64_t)size * UCLOCK_FREQ /
                        upipe_ts_encaps->octetrate;
    uint64_t peak_duration = (uint64_t)size * UCLOCK_FREQ /
                             upipe_ts_encaps->tb_rate;
    uint64_t end = dts_sys - delay;
    uint64_t begin = end - duration;

    if (upipe_ts_encaps->next_pcr != UINT64_MAX &&
        upipe_ts_encaps->next_pcr != 0 &&
        upipe_ts_encaps->next_pcr + MAX_HOLE <=
                   begin - upipe_ts_encaps->pcr_tolerance -
                   upipe_ts_encaps->ts_delay) {
        upipe_warn_va(upipe, "skipping hole in the source (%"PRIu64" ms)",
                      (begin - upipe_ts_encaps->pcr_tolerance -
                       upipe_ts_encaps->ts_delay - upipe_ts_encaps->next_pcr) *
                      1000 / UCLOCK_FREQ);
        upipe_ts_encaps->next_pcr = 0;
    }

    if (upipe_ts_encaps->next_pcr != UINT64_MAX &&
        upipe_ts_encaps->next_pcr != 0) {
        /* Send out PCRs that may be missing due to an interruption of our
         * stream */
        while (upipe_ts_encaps->next_pcr <=
                   begin - upipe_ts_encaps->pcr_tolerance -
                   upipe_ts_encaps->ts_delay) {
            struct uref *output =
                upipe_ts_encaps_pad_pcr(upipe,
                                        dts_prog - dts_sys +
                                        upipe_ts_encaps->next_pcr -
                                        upipe_ts_encaps->pcr_tolerance -
                                        upipe_ts_encaps->ts_delay);
            if (likely(output != NULL)) {
                uint64_t pcr_dts_sys = upipe_ts_encaps->next_pcr +
                                       upipe_ts_encaps->pcr_interval / 2;
                uref_clock_set_ref(output);
                uref_clock_set_cr_dts_delay(output,
                    pcr_dts_sys - upipe_ts_encaps->next_pcr +
                    upipe_ts_encaps->pcr_tolerance + upipe_ts_encaps->ts_delay);
                uref_clock_set_dts_sys(output, pcr_dts_sys);
                uref_clock_rebase_cr_sys(output);
                if (dts_prog != UINT64_MAX) {
                    uref_clock_set_dts_prog(output, dts_prog +
                                                    pcr_dts_sys - dts_sys);
                    uref_clock_rebase_cr_prog(output);
                }
                if (dts_orig != UINT64_MAX) {
                    uref_clock_set_dts_orig(output, dts_orig +
                                                    pcr_dts_sys - dts_sys);
                    uref_clock_rebase_cr_orig(output);
                }
                upipe_ts_encaps_output(upipe, output, upump_p);
            }
            upipe_ts_encaps->next_pcr += upipe_ts_encaps->pcr_interval;
        }
    }

    bool random = ubase_check(uref_flow_get_random(uref));
    if (unlikely(upipe_ts_encaps->next_pcr == 0))
        /* Insert on PCR interval change */
        upipe_ts_encaps->next_pcr = begin;

    /* Find out how many TS packets */
    unsigned int nb_pcr = 0;
    uint64_t next_pcr = upipe_ts_encaps->next_pcr;
    while (next_pcr <= end + upipe_ts_encaps->pcr_tolerance) {
        nb_pcr++;
        next_pcr += upipe_ts_encaps->pcr_interval;
    }

    size += nb_pcr * (TS_HEADER_SIZE_PCR - TS_HEADER_SIZE);
    if (discontinuity || random)
        size += TS_HEADER_SIZE_AF - TS_HEADER_SIZE;

    unsigned int nb_ts = (size + (TS_SIZE - TS_HEADER_SIZE - 1)) /
                         (TS_SIZE - TS_HEADER_SIZE);
    if (nb_ts < nb_pcr)
        nb_ts = nb_pcr;

    /* Outputs the packets */
    int i;
    for (i = nb_ts - 1; i >= 0; i--) {
        uint64_t muxdate = end - i * duration / nb_ts;
        uint64_t pcr = upipe_ts_encaps->next_pcr <=
                       muxdate + upipe_ts_encaps->pcr_tolerance ?
                       (dts_prog + muxdate - dts_sys -
                        upipe_ts_encaps->ts_delay) : 0;
        struct uref *output =
            upipe_ts_encaps_splice(upipe, uref, start, pcr,
                                   random, discontinuity);
        start = false;
        if (unlikely(output == NULL))
            /* This can happen if the last packet was only planned to contain
             * a PCR. In this case we will catch up next time. */
            break;

        /* DTS is now the latest theorical time at which we can output the
         * packet, considering the rest of the elementary stream will be output
         * at peak octet rate. */
        uint64_t output_dts = dts_sys - i * peak_duration / nb_ts;
        if (pcr &&
            upipe_ts_encaps->next_pcr + upipe_ts_encaps->pcr_interval / 2 <
                output_dts)
            output_dts = upipe_ts_encaps->next_pcr +
                         upipe_ts_encaps->pcr_interval / 2;
        uint64_t output_delay;
        if (output_dts < muxdate) {
            upipe_warn(upipe, "input is bursting above its max octet rate");
            output_delay = 0;
        } else
            output_delay = output_dts - muxdate;

        uref_clock_set_cr_dts_delay(output,
                                    output_delay + upipe_ts_encaps->ts_delay);

        /* Rebase against clock ref (== muxdate). */
        uref_clock_set_dts_sys(output, output_dts);
        uref_clock_rebase_cr_sys(output);
        if (dts_prog != UINT64_MAX) {
            uref_clock_set_dts_prog(output,
                                    dts_prog + output_dts - dts_sys);
            uref_clock_rebase_cr_prog(output);
        }
        if (dts_orig != UINT64_MAX) {
            uref_clock_set_dts_orig(output,
                                    dts_orig + output_dts - dts_sys);
            uref_clock_rebase_cr_orig(output);
        }

        if (pcr) {
            uref_clock_set_ref(output);
            upipe_ts_encaps->next_pcr += upipe_ts_encaps->pcr_interval;
            nb_pcr--;
        }

        upipe_ts_encaps_output(upipe, output, upump_p);
        random = false;
        discontinuity = false;
    }

    if (ubase_check(uref_block_size(uref, &size)) && size)
        upipe_warn_va(upipe, "failed to mux %u octets (pcr=%u)", size, nb_pcr);
    uref_free(uref);
}

/** @internal @This handles data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 * @return false if the input must be blocked
 */
static bool upipe_ts_encaps_handle(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_ts_encaps->psi = !ubase_ncmp(def, "block.mpegts.mpegtspsi.");
        uref_block_flow_get_octetrate(uref, &upipe_ts_encaps->octetrate);
        uref_ts_flow_get_tb_rate(uref, &upipe_ts_encaps->tb_rate);
        uint64_t pid;
        uref_ts_flow_get_pid(uref, &pid);
        upipe_ts_encaps->pid = pid;
        upipe_ts_encaps->ts_delay = 0;
        uref_ts_flow_get_ts_delay(uref, &upipe_ts_encaps->ts_delay);
        upipe_ts_encaps->max_delay = T_STD_MAX_RETENTION;
        uref_ts_flow_get_max_delay(uref, &upipe_ts_encaps->max_delay);
        upipe_ts_encaps->pcr_tolerance = (uint64_t)TS_SIZE * UCLOCK_FREQ /
                                         upipe_ts_encaps->octetrate;

        upipe_ts_encaps_store_flow_def(upipe, NULL);
        upipe_ts_encaps_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_ts_encaps->flow_def == NULL || upipe_ts_encaps->uref_mgr == NULL)
        return false;

    assert(upipe_ts_encaps->octetrate);

    /* FIXME For the moment we do not handle overlaps */
    if (upipe_ts_encaps->psi) {
        /* Prepend pointer_field */
        struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_encaps->ubuf_mgr, 1);
        uint8_t *buffer;
        int size = -1;
        if (unlikely(!ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer)))) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }
        assert(size == 1);
        buffer[0] = 0;
        ubuf_block_unmap(ubuf, 0);
        struct ubuf *section = uref_detach_ubuf(uref);
        uref_attach_ubuf(uref, ubuf);
        if (unlikely(!ubase_check(uref_block_append(uref, section)))) {
            ubuf_free(section);
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return true;
        }
    }

    upipe_ts_encaps_work(upipe, uref, upump_p);
    return true;
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_encaps_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    if (!upipe_ts_encaps_check_input(upipe)) {
        upipe_ts_encaps_hold_input(upipe, uref);
        upipe_ts_encaps_block_input(upipe, upump_p);
    } else if (!upipe_ts_encaps_handle(upipe, uref, upump_p)) {
        upipe_ts_encaps_hold_input(upipe, uref);
        upipe_ts_encaps_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This checks if the input may start.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_encaps_check(struct upipe *upipe, struct uref *flow_format)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    if (flow_format != NULL)
        upipe_ts_encaps_store_flow_def(upipe, flow_format);

    if (upipe_ts_encaps->flow_def == NULL)
        return UBASE_ERR_NONE;

    if (upipe_ts_encaps->uref_mgr == NULL) {
        upipe_ts_encaps_require_uref_mgr(upipe);
        return UBASE_ERR_NONE;
    }

    bool was_buffered = !upipe_ts_encaps_check_input(upipe);
    upipe_ts_encaps_output_input(upipe);
    upipe_ts_encaps_unblock_input(upipe);
    if (was_buffered && upipe_ts_encaps_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_ts_encaps_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_encaps_set_flow_def(struct upipe *upipe,
                                        struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    const char *def;
    uint64_t pid;
    uint64_t octetrate, tb_rate;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def))
    if (ubase_ncmp(def, EXPECTED_FLOW_DEF) ||
        !ubase_check(uref_block_flow_get_octetrate(flow_def, &octetrate)) ||
        !octetrate ||
        !ubase_check(uref_ts_flow_get_tb_rate(flow_def, &tb_rate)) ||
        !ubase_check(uref_ts_flow_get_pid(flow_def, &pid)))
        return UBASE_ERR_INVALID;

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def_dup,
                        "block.mpegts.%s", def + strlen(EXPECTED_FLOW_DEF)))))
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This returns the currently configured PCR interval.
 *
 * @param upipe description structure of the pipe
 * @param pcr_interval_p filled in with the PCR interval
 * @return an error code
 */
static int upipe_ts_encaps_get_pcr_interval(struct upipe *upipe,
                                            uint64_t *pcr_interval_p)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    assert(pcr_interval_p != NULL);
    *pcr_interval_p = upipe_ts_encaps->pcr_interval;
    return UBASE_ERR_NONE;
}

/** @internal @This sets the PCR interval. To cancel insertion of PCRs, set it
 * to 0.
 *
 * @param upipe description structure of the pipe
 * @param pcr_interval new PCR interval
 * @return an error code
 */
static int upipe_ts_encaps_set_pcr_interval(struct upipe *upipe,
                                            uint64_t pcr_interval)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    upipe_ts_encaps->pcr_interval = pcr_interval;
    upipe_ts_encaps->next_pcr = pcr_interval ? 0 : UINT64_MAX;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a ts encaps pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_encaps_control(struct upipe *upipe,
                                   int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UREF_MGR)
                return upipe_throw_provide_request(upipe, request);
            return upipe_ts_encaps_alloc_output_proxy(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            if (request->type == UREQUEST_UREF_MGR)
                return UBASE_ERR_NONE;
            return upipe_ts_encaps_free_output_proxy(upipe, request);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_encaps_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_encaps_set_flow_def(upipe, flow_def);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_encaps_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_encaps_set_output(upipe, output);
        }

        case UPIPE_TS_MUX_GET_PCR_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t *pcr_interval_p = va_arg(args, uint64_t *);
            return upipe_ts_encaps_get_pcr_interval(upipe, pcr_interval_p);
        }
        case UPIPE_TS_MUX_SET_PCR_INTERVAL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            uint64_t pcr_interval = va_arg(args, uint64_t);
            return upipe_ts_encaps_set_pcr_interval(upipe, pcr_interval);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_encaps_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_encaps_clean_input(upipe);
    upipe_ts_encaps_clean_output(upipe);
    upipe_ts_encaps_clean_ubuf_mgr(upipe);
    upipe_ts_encaps_clean_uref_mgr(upipe);
    upipe_ts_encaps_clean_urefcount(upipe);
    upipe_ts_encaps_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_encaps_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_ENCAPS_SIGNATURE,

    .upipe_alloc = upipe_ts_encaps_alloc,
    .upipe_input = upipe_ts_encaps_input,
    .upipe_control = upipe_ts_encaps_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_encaps_mgr_alloc(void)
{
    return &upipe_ts_encaps_mgr;
}

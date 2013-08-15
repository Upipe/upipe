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
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** T-STD standard max retention time - 1 s */
#define T_STD_MAX_RETENTION UCLOCK_FREQ

/** @internal @This is the private context of a ts_encaps pipe. */
struct upipe_ts_encaps {
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

    /** PID */
    uint16_t pid;
    /** octetrate */
    uint64_t octetrate;
    /** max octetrate (in the sense of T-STD) */
    uint64_t max_octetrate;
    /** T-STD TS delay (TB buffer) */
    uint64_t ts_delay;
    /** T-STD max retention time */
    uint64_t max_delay;
    /** true if we chop PSI sections */
    bool psi;

    /** PCR period (or 0) */
    uint64_t pcr_period;
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
UPIPE_HELPER_FLOW(upipe_ts_encaps, EXPECTED_FLOW_DEF)
UPIPE_HELPER_UREF_MGR(upipe_ts_encaps, uref_mgr)
UPIPE_HELPER_UBUF_MGR(upipe_ts_encaps, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_ts_encaps, output, flow_def, flow_def_sent)

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
    struct uref *flow_def;
    struct upipe *upipe = upipe_ts_encaps_alloc_flow(mgr, uprobe, signature,
                                                     args, &flow_def);
    if (unlikely(upipe == NULL))
        return NULL;

    const char *def;
    uref_flow_get_def(flow_def, &def);
    bool psi = !ubase_ncmp(def, "block.mpegtspsi.");

    uint64_t pid;
    uint64_t octetrate;
    if (unlikely(!uref_block_flow_get_octetrate(flow_def, &octetrate) ||
                 !uref_ts_flow_get_pid(flow_def, &pid) ||
                 !uref_flow_set_def_va(flow_def, "block.mpegts.%s",
                                       def + strlen(EXPECTED_FLOW_DEF)))) {
        uref_free(flow_def);
        upipe_ts_encaps_free_flow(upipe);
        return NULL;
    }

    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    upipe_ts_encaps_init_output(upipe);
    upipe_ts_encaps_init_uref_mgr(upipe);
    upipe_ts_encaps_init_ubuf_mgr(upipe);
    upipe_ts_encaps->pid = pid;
    upipe_ts_encaps->octetrate = octetrate;
    upipe_ts_encaps->max_octetrate = octetrate;
    uref_block_flow_get_max_octetrate(flow_def,
                                      &upipe_ts_encaps->max_octetrate);
    upipe_ts_encaps->ts_delay = 0;
    uref_ts_flow_get_ts_delay(flow_def, &upipe_ts_encaps->ts_delay);
    upipe_ts_encaps->max_delay = T_STD_MAX_RETENTION;
    uref_ts_flow_get_max_delay(flow_def, &upipe_ts_encaps->max_delay);
    upipe_ts_encaps->psi = psi;
    upipe_ts_encaps->pcr_period = 0;
    upipe_ts_encaps->last_cc = 0;
    upipe_ts_encaps->pcr_tolerance = (uint64_t)TS_SIZE * UCLOCK_FREQ /
                                     octetrate;
    upipe_ts_encaps->next_pcr = UINT64_MAX;
    upipe_ts_encaps_store_flow_def(upipe, flow_def);
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
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return NULL;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!uref_block_write(output, 0, &size, &buffer))) {
        uref_free(output);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return NULL;
    }

    ts_init(buffer);
    ts_set_pid(buffer, upipe_ts_encaps->pid);
    /* Do not increase continuity counter on packets containing no payload */
    ts_set_cc(buffer, upipe_ts_encaps->last_cc);
    ts_set_adaptation(buffer, TS_SIZE - TS_HEADER_SIZE - 1);
    tsaf_set_pcr(buffer, pcr / 300);
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

    size_t uref_size, padding_size = 0;
    uref_block_size(uref, &uref_size);
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
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
        return NULL;
    }

    uint8_t *buffer;
    int size = -1;
    if (unlikely(!uref_block_write(output, 0, &size, &buffer))) {
        uref_free(output);
        upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
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
            tsaf_set_pcr(buffer, pcr / 300);
            tsaf_set_pcrext(buffer, pcr % 300);
        }
    }

    uref_block_unmap(output, 0);

    struct ubuf *payload = ubuf_block_splice(uref->ubuf, 0,
                                         TS_SIZE - header_size - padding_size);
    if (unlikely(payload == NULL || !uref_block_append(output, payload) ||
                 !uref_block_resize(uref,
                            TS_SIZE - header_size - padding_size, -1))) {
        if (payload != NULL)
            ubuf_free(payload);
        uref_free(output);
        return NULL;
    }

    if (padding_size) {
        /* With PSI, pad with 0xff */
        struct ubuf *padding = ubuf_block_alloc(upipe_ts_encaps->ubuf_mgr,
                                                padding_size);
        size = -1;
        if (unlikely(padding == NULL ||
                     !ubuf_block_write(padding, 0, &size, &buffer))) {
            uref_free(output);
            return NULL;
        }
        memset(buffer, 0xff, size);
        ubuf_block_unmap(padding, 0);

        if (unlikely(!uref_block_append(output, padding))) {
            ubuf_free(padding);
            uref_free(output);
            return NULL;
        }
    }

    return output;
}

/** @internal @This chops the access unit in TS packets and adds TS header.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_encaps_work(struct upipe *upipe, struct uref *uref,
                                 struct upump *upump)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    uint64_t dts, dts_sys = UINT64_MAX, dts_orig = UINT64_MAX, delay = 0;
    if (!uref_clock_get_dts(uref, &dts)) {
        upipe_warn_va(upipe, "non-dated packet received");
        uref_free(uref);
        return;
    }
    uref_clock_get_dts_sys(uref, &dts_sys);
    uref_clock_get_dts_orig(uref, &dts_orig);
    uref_clock_get_vbv_delay(uref, &delay);

    if (delay > upipe_ts_encaps->max_delay)
        /* TODO what do we do then ? */
        upipe_warn_va(upipe,
                      "input exceeds T-STD max retention time (%"PRIu64" ms)",
                      delay * 1000 / UCLOCK_FREQ);

    size_t size;
    if (!uref_block_size(uref, &size) || !size) {
        upipe_warn_va(upipe, "empty packet received");
        uref_free(uref);
        return;
    }

    bool discontinuity = uref_flow_get_discontinuity(uref);

    uint64_t duration = (uint64_t)size * UCLOCK_FREQ /
                        upipe_ts_encaps->octetrate;
    uint64_t peak_duration = (uint64_t)size * UCLOCK_FREQ /
                             upipe_ts_encaps->max_octetrate;
    uint64_t end = dts - delay;
    uint64_t begin = end - duration;

    if (upipe_ts_encaps->next_pcr != UINT64_MAX &&
        upipe_ts_encaps->next_pcr != 0) {
        /* Send out PCRs that may be missing due to an interruption of our
         * stream */
        while (upipe_ts_encaps->next_pcr <=
                   begin + upipe_ts_encaps->pcr_tolerance) {
            struct uref *output = upipe_ts_encaps_pad_pcr(upipe,
                                              upipe_ts_encaps->next_pcr);
            if (likely(output != NULL)) {
                uref_clock_set_ref(output);
                uref_clock_set_dts(output, upipe_ts_encaps->next_pcr);
                if (dts_sys != UINT64_MAX)
                    uref_clock_set_dts_sys(output,
                            dts_sys - (dts - upipe_ts_encaps->next_pcr));
                if (dts_orig != UINT64_MAX)
                    uref_clock_set_dts_orig(output,
                            dts_orig - (dts - upipe_ts_encaps->next_pcr));
                uref_clock_set_vbv_delay(output,
                                         upipe_ts_encaps->pcr_tolerance);
                upipe_ts_encaps_output(upipe, output, upump);
            }
            upipe_ts_encaps->next_pcr += upipe_ts_encaps->pcr_period;
        }
    }

    bool random = uref_flow_get_random(uref);
    if (unlikely(upipe_ts_encaps->next_pcr != UINT64_MAX &&
                 (random || upipe_ts_encaps->next_pcr == 0)))
        /* Insert PCR on key frames and on PCR period change */
        upipe_ts_encaps->next_pcr = begin;

    /* Find out how many TS packets */
    unsigned int nb_pcr = 0;
    uint64_t next_pcr = upipe_ts_encaps->next_pcr;
    while (next_pcr <= end + upipe_ts_encaps->pcr_tolerance) {
        nb_pcr++;
        next_pcr += upipe_ts_encaps->pcr_period;
    }

    size += nb_pcr * (TS_HEADER_SIZE_PCR - TS_HEADER_SIZE);
    if (discontinuity || random)
        size += TS_HEADER_SIZE_AF - TS_HEADER_SIZE;

    unsigned int nb_ts = (size + (TS_SIZE - TS_HEADER_SIZE - 1)) /
                         (TS_SIZE - TS_HEADER_SIZE);
    if (nb_ts < nb_pcr)
        nb_ts = nb_pcr;

    /* Outputs the packets */
    unsigned int i;
    for (i = nb_ts - 1; i >= 0; i--) {
        uint64_t muxdate = end - i * duration / nb_ts;
        bool pcr = upipe_ts_encaps->next_pcr <=
                       muxdate + upipe_ts_encaps->pcr_tolerance;
        bool ret = true;

        struct uref *output =
            upipe_ts_encaps_splice(upipe, uref, i == nb_ts - 1,
                                   pcr ? muxdate : 0, random, discontinuity);
        if (unlikely(output == NULL))
            break;

        /* DTS is now the latest theorical time at which we can output the
         * packet, considering the rest of the elementary stream will be output
         * at peak octet rate. */
        uint64_t output_dts = dts - i * peak_duration / nb_ts;
        ret = ret && uref_clock_set_dts(output, output_dts);
        if (dts_sys != UINT64_MAX)
            ret = ret && uref_clock_set_dts_sys(output,
                                        dts_sys - i * peak_duration / nb_ts);
        if (dts_orig != UINT64_MAX)
            ret = ret && uref_clock_set_dts_orig(output,
                                        dts_orig - i * peak_duration / nb_ts);

        /* DTS - delay is the theorical muxing date */
        uint64_t output_delay;
        if (output_dts + upipe_ts_encaps->ts_delay < muxdate) {
            upipe_warn(upipe, "input is bursting above its max octet rate");
            output_delay = 0;
        } else
            output_delay = output_dts + upipe_ts_encaps->ts_delay - muxdate;
        if (output_delay)
            ret = ret && uref_clock_set_vbv_delay(output, output_delay);

        if (pcr) {
            ret = ret && uref_clock_set_ref(output);
            upipe_ts_encaps->next_pcr = muxdate + upipe_ts_encaps->pcr_period;
        }

        if (!ret) {
            uref_free(output);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            break;
        }

        upipe_ts_encaps_output(upipe, output, upump);
        random = false;
        discontinuity = false;
    }

    if (uref_block_size(uref, &size) && size)
        upipe_warn_va(upipe, "failed to mux %u octets", size);
    uref_free(uref);
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_encaps_input(struct upipe *upipe, struct uref *uref,
                                  struct upump *upump)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    if (unlikely(uref->ubuf == NULL)) {
        upipe_ts_encaps_output(upipe, uref, upump);
        return;
    }

    if (unlikely(upipe_ts_encaps->uref_mgr == NULL))
        upipe_throw_need_uref_mgr(upipe);
    if (unlikely(upipe_ts_encaps->uref_mgr == NULL)) {
        uref_free(uref);
        return;
    }
    if (unlikely(upipe_ts_encaps->ubuf_mgr == NULL))
        upipe_throw_need_ubuf_mgr(upipe, upipe_ts_encaps->flow_def);
    if (unlikely(upipe_ts_encaps->ubuf_mgr == NULL)) {
        uref_free(uref);
        return;
    }

    /* FIXME For the moment we do not handle overlaps */
    if (upipe_ts_encaps->psi) {
        /* Prepend pointer_field */
        struct ubuf *ubuf = ubuf_block_alloc(upipe_ts_encaps->ubuf_mgr, 1);
        uint8_t *buffer;
        int size = -1;
        if (unlikely(!ubuf_block_write(ubuf, 0, &size, &buffer))) {
            ubuf_free(ubuf);
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }
        assert(size == 1);
        buffer[0] = 0;
        ubuf_block_unmap(ubuf, 0);
        struct ubuf *section = uref_detach_ubuf(uref);
        uref_attach_ubuf(uref, ubuf);
        if (unlikely(!uref_block_append(uref, section))) {
            ubuf_free(section);
            uref_free(uref);
            upipe_throw_fatal(upipe, UPROBE_ERR_ALLOC);
            return;
        }
    }

    upipe_ts_encaps_work(upipe, uref, upump);
}

/** @internal @This returns the currently configured PCR period.
 *
 * @param upipe description structure of the pipe
 * @param pcr_period_p filled in with the PCR period
 * @return false in case of error
 */
static bool _upipe_ts_encaps_get_pcr_period(struct upipe *upipe,
                                            uint64_t *pcr_period_p)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    assert(pcr_period_p != NULL);
    *pcr_period_p = upipe_ts_encaps->pcr_period;
    return true;
}

/** @internal @This sets the PCR period. To cancel insertion of PCRs, set it
 * to 0.
 *
 * @param upipe description structure of the pipe
 * @param pcr_period new PCR period
 * @return false in case of error
 */
static bool _upipe_ts_encaps_set_pcr_period(struct upipe *upipe,
                                            uint64_t pcr_period)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    upipe_ts_encaps->pcr_period = pcr_period;
    upipe_ts_encaps->next_pcr = pcr_period ? 0 : UINT64_MAX;
    return true;
}

/** @internal @This processes control commands on a ts encaps pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_ts_encaps_control(struct upipe *upipe,
                                    enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_ts_encaps_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_ts_encaps_set_uref_mgr(upipe, uref_mgr);
        }
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_ts_encaps_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_ts_encaps_set_ubuf_mgr(upipe, ubuf_mgr);
        }

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_ts_encaps_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_encaps_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_ts_encaps_set_output(upipe, output);
        }

        case UPIPE_TS_ENCAPS_GET_PCR_PERIOD: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_ENCAPS_SIGNATURE);
            uint64_t *pcr_period_p = va_arg(args, uint64_t *);
            return _upipe_ts_encaps_get_pcr_period(upipe, pcr_period_p);
        }
        case UPIPE_TS_ENCAPS_SET_PCR_PERIOD: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_TS_ENCAPS_SIGNATURE);
            uint64_t pcr_period = va_arg(args, uint64_t);
            return _upipe_ts_encaps_set_pcr_period(upipe, pcr_period);
        }
        default:
            return false;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_encaps_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_ts_encaps_clean_output(upipe);
    upipe_ts_encaps_clean_ubuf_mgr(upipe);
    upipe_ts_encaps_clean_uref_mgr(upipe);
    upipe_ts_encaps_free_flow(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_encaps_mgr = {
    .signature = UPIPE_TS_ENCAPS_SIGNATURE,

    .upipe_alloc = upipe_ts_encaps_alloc,
    .upipe_input = upipe_ts_encaps_input,
    .upipe_control = upipe_ts_encaps_control,
    .upipe_free = upipe_ts_encaps_free,

    .upipe_mgr_free = NULL
};

/** @This returns the management structure for all ts_encaps pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_encaps_mgr_alloc(void)
{
    return &upipe_ts_encaps_mgr;
}

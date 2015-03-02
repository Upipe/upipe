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
#include <bitstream/mpeg/pes.h>

/** we only accept blocks */
#define EXPECTED_FLOW_DEF "block."
/** flow definition for PSI */
#define FLOW_DEF_PSI "block.mpegtspsi."
/** 2^33 (max resolution of PCR, PTS and DTS) */
#define POW2_33 UINT64_C(8589934592)
/** ratio between Upipe freq and MPEG PES freq */
#define SCALE_33 (UCLOCK_FREQ / 90000)
/** T-STD standard max retention time - 1 s */
#define T_STD_MAX_RETENTION UCLOCK_FREQ
/** PID for padding stream */
#define PADDING_PID 8191

/** @hidden */
static int upipe_ts_encaps_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_encaps pipe. */
struct upipe_ts_encaps {
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
    /** output flow definition packet */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** current uref being worked on */
    struct uref *uref;
    /** true if this is the start of an access unit */
    bool start;
    /** true if this is a random access point */
    bool random;
    /** true if there was a discontinuity */
    bool discontinuity;
    /** temporary uref storage */
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

    /** PES stream ID */
    uint8_t pes_id;
    /** minimum PES header size */
    uint8_t pes_header_size;
    /** minimum PES duration */
    uint64_t pes_min_duration;

    /** a padding packet for PSI streams */
    struct ubuf *padding;
    /** last continuity counter for this PID */
    uint8_t last_cc;
    /** last time prepare was called */
    uint64_t last_prepare;
    /** muxing date of the last PCR */
    uint64_t last_pcr;
    /** offset between cr_sys and cr_prog */
    int64_t pcr_prog_offset;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_encaps, upipe, UPIPE_TS_ENCAPS_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_encaps, urefcount, upipe_ts_encaps_free)
UPIPE_HELPER_VOID(upipe_ts_encaps)
UPIPE_HELPER_OUTPUT(upipe_ts_encaps, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_encaps, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_encaps_check,
                      upipe_ts_encaps_register_output_request,
                      upipe_ts_encaps_unregister_output_request)
UPIPE_HELPER_INPUT(upipe_ts_encaps, urefs, nb_urefs, max_urefs, blockers, NULL)

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
    upipe_ts_encaps_init_ubuf_mgr(upipe);
    upipe_ts_encaps_init_input(upipe);
    upipe_ts_encaps->uref = NULL;
    upipe_ts_encaps->start = true;
    upipe_ts_encaps->random = true;
    upipe_ts_encaps->discontinuity = true;
    upipe_ts_encaps->pid = 8192;
    upipe_ts_encaps->octetrate = 0;
    upipe_ts_encaps->tb_rate = 0;
    upipe_ts_encaps->ts_delay = 0;
    upipe_ts_encaps->max_delay = T_STD_MAX_RETENTION;
    upipe_ts_encaps->psi = false;
    upipe_ts_encaps->pcr_interval = 0;
    upipe_ts_encaps->pes_id = 0;
    upipe_ts_encaps->pes_header_size = 0;
    upipe_ts_encaps->pes_min_duration = 0;
    upipe_ts_encaps->padding = NULL;
    upipe_ts_encaps->last_cc = 0;
    upipe_ts_encaps->last_prepare = 0;
    upipe_ts_encaps->last_pcr = 0;
    upipe_ts_encaps->pcr_prog_offset = INT64_MAX;
    upipe_throw_ready(upipe);
    return upipe;
}

/** @This promotes a uref to the temporary buffer, checking for flow def
 * changes.
 *
 * @param upipe description structure of the pipe
 * @return true if a flow def change was handled
 */
static bool upipe_ts_encaps_promote_uref(struct upipe *upipe)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    bool flow_def_changed = false;

    while (upipe_ts_encaps->uref == NULL) {
        struct uref *uref = upipe_ts_encaps_pop_input(upipe);
        if (uref == NULL)
            break;

        const char *def;
        uint64_t cr_prog, cr_sys;
        if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
            upipe_ts_encaps->psi = !ubase_ncmp(def, "block.mpegts.mpegtspsi.");
            uref_flow_set_def(uref, "void.");
            uref_block_flow_get_octetrate(uref, &upipe_ts_encaps->octetrate);
            uref_ts_flow_get_tb_rate(uref, &upipe_ts_encaps->tb_rate);
            uint64_t pid = PADDING_PID;
            uref_ts_flow_get_pid(uref, &pid);
            upipe_ts_encaps->pid = pid;
            upipe_ts_encaps->ts_delay = 0;
            uref_ts_flow_get_ts_delay(uref, &upipe_ts_encaps->ts_delay);
            upipe_ts_encaps->max_delay = T_STD_MAX_RETENTION;
            uref_ts_flow_get_max_delay(uref, &upipe_ts_encaps->max_delay);
            if (ubase_ncmp(def, FLOW_DEF_PSI))
                uref_ts_flow_get_pes_id(uref, &upipe_ts_encaps->pes_id);

            upipe_ts_encaps_store_flow_def(upipe, uref);
            /* trigger set_flow_def */
            upipe_ts_encaps_output(upipe, NULL, NULL);
            flow_def_changed = true;

        } else if (unlikely(!ubase_check(uref_clock_get_cr_sys(uref,
                                                &cr_sys)) ||
                            (upipe_ts_encaps->pcr_interval &&
                             !ubase_check(uref_clock_get_cr_prog(uref,
                                                &cr_prog))))) {
            upipe_warn(upipe, "dropping non-dated packet");
            uref_free(uref);

        } else {
            upipe_ts_encaps->uref = uref;
            if (upipe_ts_encaps->pcr_interval)
                upipe_ts_encaps->pcr_prog_offset = cr_prog - cr_sys;
            else
                upipe_ts_encaps->pcr_prog_offset = INT64_MAX;
            upipe_ts_encaps->start = true;
            upipe_ts_encaps->random = ubase_check(uref_flow_get_random(uref));
            upipe_ts_encaps->discontinuity =
                ubase_check(uref_flow_get_discontinuity(uref));
        }
    }
    return flow_def_changed;
}

/** @This consumes a uref and promotes a new one. Protect this with
 * @ref upipe_use/@ref upipe_release.
 *
 * @param upipe description structure of the pipe
 * @return true if a flow def change was handled
 */
static bool upipe_ts_encaps_consume_uref(struct upipe *upipe)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    uref_free(upipe_ts_encaps->uref);
    upipe_ts_encaps->uref = NULL;
    return upipe_ts_encaps_promote_uref(upipe);
}

/** @internal @This receives and queues data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_encaps_input(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p)
{
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);

    if (unlikely(upipe_ts_encaps->max_urefs &&
                 upipe_ts_encaps->nb_urefs >= upipe_ts_encaps->max_urefs)) {
        upipe_dbg(upipe, "too many queued packets, dropping");
        uref_free(uref);
        return;
    }

    upipe_ts_encaps_hold_input(upipe, uref);
    upipe_ts_encaps_promote_uref(upipe);
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
        uref_free(flow_format);

    if (upipe_ts_encaps->ubuf_mgr != NULL && upipe_ts_encaps->padding == NULL) {
        struct ubuf *padding = ubuf_block_alloc(upipe_ts_encaps->ubuf_mgr,
                                                TS_SIZE - TS_HEADER_SIZE);
        uint8_t *buffer;
        int size = -1;
        if (unlikely(padding == NULL ||
                     !ubase_check(ubuf_block_write(padding, 0,
                                                   &size, &buffer)))) {
            ubuf_free(padding);
            return UBASE_ERR_ALLOC;
        }
        memset(buffer, 0xff, size);
        ubuf_block_unmap(padding, 0);
        upipe_ts_encaps->padding = padding;
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

    uint8_t pes_id;
    if (ubase_ncmp(def, FLOW_DEF_PSI)) {
        if (!ubase_check(uref_ts_flow_get_pes_id(flow_def, &pes_id)))
            return UBASE_ERR_INVALID;
    }

    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (ubase_ncmp(def, FLOW_DEF_PSI)) {
        if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def_dup,
                            "block.mpegts.mpegtspes.%s",
                            def + strlen(EXPECTED_FLOW_DEF)))))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    } else {
        if (unlikely(!ubase_check(uref_flow_set_def_va(flow_def_dup,
                            "block.mpegts.%s",
                            def + strlen(EXPECTED_FLOW_DEF)))))
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
    }
    upipe_input(upipe, flow_def_dup, NULL);

    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    if (unlikely(upipe_ts_encaps->ubuf_mgr == NULL)) {
        if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
        upipe_ts_encaps_require_ubuf_mgr(upipe, flow_def_dup);
    }
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
    return UBASE_ERR_NONE;
}

/** @This returns the date of the next access unit.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys_p filled in with the date of the next access unit
 * @return an error code
 */
static int _upipe_ts_encaps_peek(struct upipe *upipe, uint64_t *cr_sys_p)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    if (unlikely(encaps->ubuf_mgr == NULL))
        return UBASE_ERR_UNHANDLED;

    for ( ; ; ) {
        if (encaps->uref == NULL)
            return UBASE_ERR_UNHANDLED;

        if (unlikely(!ubase_check(uref_clock_get_cr_sys(encaps->uref,
                                                        cr_sys_p)))) {
            upipe_warn(upipe, "dropping non-dated packet");
            upipe_ts_encaps_consume_uref(upipe);
            continue;
        }

        return UBASE_ERR_NONE;
    }
}

/** @This returns the date of the next TS packet, and deletes all data
 * prior the given date.
 *
 * @param upipe description structure of the pipe
 * @param cr_sys data before cr_sys will be deleted
 * @param cr_sys_p filled in with the date of the next TS packet
 * @param dts_sys_p filled in with the DTS of the next TS packet
 * @return an error code
 */
static int _upipe_ts_encaps_prepare(struct upipe *upipe, uint64_t cr_sys,
                                    uint64_t *cr_sys_p, uint64_t *dts_sys_p)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    if (unlikely(encaps->ubuf_mgr == NULL))
        return UBASE_ERR_UNHANDLED;

    encaps->last_prepare = cr_sys;

    if (encaps->pcr_interval && encaps->pcr_prog_offset != INT64_MAX &&
        encaps->last_pcr + encaps->pcr_interval <= cr_sys) {
        *cr_sys_p = cr_sys;
        *dts_sys_p = cr_sys;
        return UBASE_ERR_NONE;
    }

    for ( ; ; ) {
        if (encaps->uref == NULL)
            return UBASE_ERR_UNHANDLED;

        uint64_t uref_cr_sys;
        if (unlikely(!ubase_check(uref_clock_get_cr_sys(encaps->uref,
                                                        &uref_cr_sys)))) {
            upipe_warn(upipe, "dropping non-dated packet");
            upipe_ts_encaps_consume_uref(upipe);
            continue;
        }

        uint64_t uref_dts_sys;
        if (uref_cr_sys > cr_sys ||
            !ubase_check(uref_clock_get_dts_sys(encaps->uref, &uref_dts_sys))) {
            *cr_sys_p = uref_cr_sys;
            *dts_sys_p = UINT64_MAX;
            return UBASE_ERR_NONE;
        }

        size_t size;
        if (!ubase_check(uref_block_size(encaps->uref, &size)) || !size) {
            upipe_warn_va(upipe, "dropping empty packet");
            upipe_ts_encaps_consume_uref(upipe);
            continue;
        }

        uint64_t peak_duration = (uint64_t)size * UCLOCK_FREQ / encaps->tb_rate;
        if (unlikely(uref_dts_sys - peak_duration < cr_sys)) {
            upipe_warn_va(upipe, "dropping late packet (%"PRIu64")",
                          cr_sys - (uref_dts_sys - peak_duration));
            upipe_ts_encaps_consume_uref(upipe);
            continue;
        }

        *cr_sys_p = uref_cr_sys;
        *dts_sys_p = uref_dts_sys - peak_duration;
        return UBASE_ERR_NONE;
    }
}

/** @internal @This builds a PES header.
 *
 * @param upipe description structure of the pipe
 * @param payload_size size of the payload
 * @param alignement true if the data alignment flag must be set
 * @param pts_prog value of the PTS field, in 27 MHz units
 * @param dts_prog value of the DTS field, in 27 MHz units
 * @return allocated PES header
 */
static struct ubuf *upipe_ts_encaps_build_pes(struct upipe *upipe,
                                              size_t payload_size,
                                              bool alignment,
                                              uint64_t pts_prog,
                                              uint64_t dts_prog)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    size_t header_size;
    if (encaps->pes_id != PES_STREAM_ID_PRIVATE_2) {
        if (pts_prog != UINT64_MAX) {
            if (dts_prog != UINT64_MAX &&
                ((pts_prog / SCALE_33) % POW2_33) !=
                    ((dts_prog / SCALE_33) % POW2_33))
                header_size = PES_HEADER_SIZE_PTSDTS;
            else
                header_size = PES_HEADER_SIZE_PTS;
        } else
            header_size = PES_HEADER_SIZE_NOPTS;
    } else
        header_size = PES_HEADER_SIZE;
    if (header_size < encaps->pes_header_size)
        header_size = encaps->pes_header_size;

    upipe_verbose_va(upipe, "preparing PES header (size %zu)", header_size);
    struct ubuf *ubuf = ubuf_block_alloc(encaps->ubuf_mgr, header_size);
    uint8_t *buffer;
    int size = -1;
    if (unlikely(ubuf == NULL ||
                 !ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer)))) {
        ubuf_free(ubuf);
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return NULL;
    }

    pes_init(buffer);
    pes_set_streamid(buffer, encaps->pes_id);
    size_t pes_length = payload_size + header_size - PES_HEADER_SIZE;
    if (pes_length > UINT16_MAX) {
        if (unlikely((encaps->pes_id & PES_STREAM_ID_VIDEO_MPEG) !=
                     PES_STREAM_ID_VIDEO_MPEG))
            upipe_warn(upipe, "PES length > 65535 for a non-video stream");
        pes_set_length(buffer, 0);
    } else
        pes_set_length(buffer, pes_length);

    if (encaps->pes_id != PES_STREAM_ID_PRIVATE_2) {
        pes_set_headerlength(buffer, header_size - PES_HEADER_SIZE_NOPTS);
        if (alignment)
            pes_set_dataalignment(buffer);
        if (pts_prog != UINT64_MAX) {
            pes_set_pts(buffer, (pts_prog / SCALE_33) % POW2_33);
            if (dts_prog != UINT64_MAX &&
                ((pts_prog / SCALE_33) % POW2_33) !=
                    ((dts_prog / SCALE_33) % POW2_33))
                pes_set_dts(buffer, (dts_prog / SCALE_33) % POW2_33);
        }
    }

    ubuf_block_unmap(ubuf, 0);
    return ubuf;
}

/** @internal @This prepares a new access unit for splicing.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_ts_encaps_promote_au(struct upipe *upipe)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    /* TODO buffer overlap */
    if (encaps->psi) {
        /* Prepend pointer_field */
        upipe_verbose_va(upipe, "preparing PSI pointer_field");
        struct ubuf *ubuf = ubuf_block_alloc(encaps->ubuf_mgr, 1);
        uint8_t *buffer;
        int size = -1;
        if (unlikely(ubuf == NULL ||
                     !ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer)))) {
            ubuf_free(ubuf);
            return UBASE_ERR_ALLOC;
        }
        assert(size == 1);
        buffer[0] = 0;
        ubuf_block_unmap(ubuf, 0);
        struct ubuf *section = uref_detach_ubuf(encaps->uref);
        uref_attach_ubuf(encaps->uref, ubuf);
        if (unlikely(!ubase_check(uref_block_append(encaps->uref, section)))) {
            ubuf_free(section);
            return UBASE_ERR_ALLOC;
        }
        return UBASE_ERR_NONE;
    }

    size_t uref_size;
    UBASE_RETURN(uref_block_size(encaps->uref, &uref_size));

    uint64_t pts_prog = UINT64_MAX, dts_prog = UINT64_MAX;
    uref_clock_get_pts_prog(encaps->uref, &pts_prog);
    uref_clock_get_dts_prog(encaps->uref, &dts_prog);
    struct ubuf *ubuf =
        upipe_ts_encaps_build_pes(upipe, uref_size, true, pts_prog, dts_prog);
    struct ubuf *section = uref_detach_ubuf(encaps->uref);
    uref_attach_ubuf(encaps->uref, ubuf);
    if (unlikely(!ubase_check(uref_block_append(encaps->uref, section)))) {
        ubuf_free(section);
        return UBASE_ERR_ALLOC;
    }
    return UBASE_ERR_NONE;
}

/** @internal @This builds a TS header.
 *
 * @param upipe description structure of the pipe
 * @param payload_size available size of the payload
 * @param start true if it's the first packet of the access unit
 * @param pcr_prog value of the PCR field, in 27 MHz units, or UINT64_MAX
 * @param random true if the packet is a random access point
 * @param discontinuity true if the packet must have the discontinuity flag
 * @return allocated TS header
 */
static struct ubuf *upipe_ts_encaps_build_ts(struct upipe *upipe,
                                             size_t payload_size, bool start,
                                             uint64_t pcr_prog, bool random,
                                             bool discontinuity)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    size_t header_size;
    if (unlikely(pcr_prog != UINT64_MAX))
        header_size = TS_HEADER_SIZE_PCR;
    else if (unlikely(discontinuity || random))
        header_size = TS_HEADER_SIZE_AF;
    else
        header_size = TS_HEADER_SIZE;

    if (!encaps->psi && payload_size < TS_SIZE - header_size)
        header_size = TS_SIZE - payload_size;

    upipe_verbose_va(upipe, "preparing TS header (size %zu)", header_size);
    struct ubuf *ubuf = ubuf_block_alloc(encaps->ubuf_mgr, header_size);
    uint8_t *buffer;
    int size = -1;
    if (unlikely(ubuf == NULL ||
                 !ubase_check(ubuf_block_write(ubuf, 0, &size, &buffer)))) {
        ubuf_free(ubuf);
        return NULL;
    }
    assert(size == header_size);

    ts_init(buffer);
    ts_set_pid(buffer, encaps->pid);
    if (payload_size) {
        encaps->last_cc++;
        encaps->last_cc &= 0xf;
        ts_set_payload(buffer);
    }
    ts_set_cc(buffer, encaps->last_cc);
    if (start)
        ts_set_unitstart(buffer);

    if (header_size > TS_HEADER_SIZE) {
        ts_set_adaptation(buffer, header_size - TS_HEADER_SIZE - 1);
        if (discontinuity)
            tsaf_set_discontinuity(buffer);
        if (random)
            tsaf_set_randomaccess(buffer);
        if (pcr_prog != UINT64_MAX) {
            tsaf_set_pcr(buffer, (pcr_prog / SCALE_33) % POW2_33);
            tsaf_set_pcrext(buffer, pcr_prog % SCALE_33);
        }
    }

    ubuf_block_unmap(ubuf, 0);
    return ubuf;
}

/** @internal @This splices the input uref and appends to the given ubuf to
 * build a complete TS packet. For PSI sections it may also append padding.
 *
 * @param upipe description structure of the pipe
 * @param ubuf_p appended with the payload of the packet
 * @param dts_sys_p filled in with the DTS, or UINT64_MAX
 * @return an error code
 */
static int upipe_ts_encaps_complete(struct upipe *upipe, struct ubuf **ubuf_p,
                                    uint64_t *dts_sys_p)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    size_t uref_size;
    UBASE_RETURN(uref_block_size(encaps->uref, &uref_size));

    size_t ubuf_size;
    UBASE_RETURN(ubuf_block_size(*ubuf_p, &ubuf_size));

    size_t payload_size = uref_size < TS_SIZE - ubuf_size ?
                          uref_size : TS_SIZE - ubuf_size;
    struct ubuf *payload = ubuf_block_splice(encaps->uref->ubuf, 0,
                                             payload_size);
    if (unlikely(payload == NULL ||
                 !ubase_check(ubuf_block_append(*ubuf_p, payload)) ||
                 !ubase_check(uref_block_resize(encaps->uref,
                                                payload_size, -1)))) {
        ubuf_free(payload);
        ubuf_free(*ubuf_p);
        return UBASE_ERR_ALLOC;
    }

    if (ubuf_size + payload_size < TS_SIZE) {
        /* With PSI, pad with 0xff */
        struct ubuf *padding = ubuf_dup(encaps->padding);
        if (unlikely(padding == NULL ||
                     !ubase_check(ubuf_block_resize(padding, 0,
                             TS_SIZE - ubuf_size - payload_size)) ||
                     !ubase_check(ubuf_block_append(*ubuf_p, padding)))) {
            ubuf_free(padding);
            ubuf_free(*ubuf_p);
            return UBASE_ERR_ALLOC;
        }
    }

    uint64_t dts_sys = *dts_sys_p = UINT64_MAX;
    uref_clock_get_dts_sys(encaps->uref, &dts_sys);

    uint64_t cr_sys;
    UBASE_RETURN(uref_clock_get_cr_sys(encaps->uref, &cr_sys));
    cr_sys += (uint64_t)payload_size * UCLOCK_FREQ / encaps->octetrate;
    uref_clock_set_cr_sys(encaps->uref, cr_sys);

    if (dts_sys != UINT64_MAX) {
        uref_clock_set_cr_dts_delay(encaps->uref, dts_sys - cr_sys);
        *dts_sys_p = dts_sys - (uint64_t)uref_size * UCLOCK_FREQ / encaps->tb_rate;
    }

    return UBASE_ERR_NONE;
}

/** @This returns a ubuf containing a TS packet, and the DTS of the packet.
 *
 * @param upipe description structure of the pipe
 * @param ubuf_p filled in with a pointer to the ubuf
 * @param dts_sys_p filled in with the DTS, or UINT64_MAX
 * @return an error code
 */
static int _upipe_ts_encaps_splice(struct upipe *upipe, struct ubuf **ubuf_p,
                                   uint64_t *dts_sys_p)
{
    struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
    if (encaps->ubuf_mgr == NULL)
        return UBASE_ERR_INVALID;

    uint64_t pcr_prog = UINT64_MAX;
    if (encaps->pcr_interval && encaps->pcr_prog_offset != INT64_MAX &&
        encaps->last_pcr + encaps->pcr_interval <= encaps->last_prepare) {
        pcr_prog = encaps->last_prepare + encaps->pcr_prog_offset;
        encaps->last_pcr = encaps->last_prepare;
    }

    if (encaps->uref == NULL) {
        if (unlikely(pcr_prog == UINT64_MAX))
            return UBASE_ERR_INVALID;

        *ubuf_p = upipe_ts_encaps_build_ts(upipe, 0, false, pcr_prog, false,
                                           false);
        *dts_sys_p = encaps->last_prepare;
        return UBASE_ERR_NONE;
    }

    if (encaps->start) {
        UBASE_RETURN(upipe_ts_encaps_promote_au(upipe));
    }

    size_t uref_size;
    UBASE_RETURN(uref_block_size(encaps->uref, &uref_size));

    *ubuf_p = upipe_ts_encaps_build_ts(upipe, uref_size, encaps->start,
            pcr_prog, encaps->random, encaps->discontinuity);

    UBASE_RETURN(upipe_ts_encaps_complete(upipe, ubuf_p, dts_sys_p));
    if (pcr_prog != UINT64_MAX)
        *dts_sys_p = encaps->last_prepare;
    encaps->start = false;
    encaps->random = false;
    encaps->discontinuity = false;

    UBASE_RETURN(uref_block_size(encaps->uref, &uref_size));
    if (!uref_size)
        upipe_ts_encaps_consume_uref(upipe);

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
        case UPIPE_GET_MAX_LENGTH: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_ts_encaps_get_max_length(upipe, p);
        }
        case UPIPE_SET_MAX_LENGTH: {
            unsigned int max_length = va_arg(args, unsigned int);
            return upipe_ts_encaps_set_max_length(upipe, max_length);
        }
        case UPIPE_FLUSH:
            return upipe_ts_encaps_flush_input(upipe);

        case UPIPE_TS_MUX_GET_CC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
            unsigned int *cc_p = va_arg(args, unsigned int *);
            assert(cc_p != NULL);
            *cc_p = encaps->last_cc;
            return UBASE_ERR_NONE;
        }
        case UPIPE_TS_MUX_SET_CC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            struct upipe_ts_encaps *encaps = upipe_ts_encaps_from_upipe(upipe);
            encaps->last_cc = va_arg(args, unsigned int);
            return UBASE_ERR_NONE;
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
        case UPIPE_TS_ENCAPS_PEEK: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_ENCAPS_SIGNATURE)
            uint64_t *cr_sys_p = va_arg(args, uint64_t *);
            return _upipe_ts_encaps_peek(upipe, cr_sys_p);
        }
        case UPIPE_TS_ENCAPS_PREPARE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_ENCAPS_SIGNATURE)
            uint64_t cr_sys = va_arg(args, uint64_t);
            uint64_t *cr_sys_p = va_arg(args, uint64_t *);
            uint64_t *dts_sys_p = va_arg(args, uint64_t *);
            return _upipe_ts_encaps_prepare(upipe, cr_sys, cr_sys_p, dts_sys_p);
        }
        case UPIPE_TS_ENCAPS_SPLICE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_ENCAPS_SIGNATURE)
            struct ubuf **ubuf_p = va_arg(args, struct ubuf **);
            uint64_t *dts_sys_p = va_arg(args, uint64_t *);
            return _upipe_ts_encaps_splice(upipe, ubuf_p, dts_sys_p);
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
    struct upipe_ts_encaps *upipe_ts_encaps = upipe_ts_encaps_from_upipe(upipe);
    upipe_throw(upipe, UPROBE_TS_MUX_LAST_CC, UPIPE_TS_MUX_SIGNATURE,
                (unsigned int)upipe_ts_encaps->last_cc);
    upipe_throw_dead(upipe);

    uref_free(upipe_ts_encaps->uref);
    ubuf_free(upipe_ts_encaps->padding);
    upipe_ts_encaps_clean_input(upipe);
    upipe_ts_encaps_clean_output(upipe);
    upipe_ts_encaps_clean_ubuf_mgr(upipe);
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

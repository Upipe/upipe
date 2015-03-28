/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short unit tests for TS encaps module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/uclock.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_encaps.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/pes.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

static unsigned int last_cc;
static uint64_t next_cr_sys = UINT64_MAX;
static uint64_t next_dts_sys = UINT64_MAX;
static uint64_t next_pcr_sys = UINT64_MAX;
static bool next_ready = false;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_NEED_OUTPUT:
            break;
        case UPROBE_TS_MUX_LAST_CC:
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            assert(last_cc == va_arg(args, unsigned int));
            break;
        case UPROBE_TS_ENCAPS_STATUS:
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_ENCAPS_SIGNATURE)
            next_cr_sys = va_arg(args, uint64_t);
            next_dts_sys = va_arg(args, uint64_t);
            next_pcr_sys = va_arg(args, uint64_t);
            next_ready = !!va_arg(args, int);
            break;
    }
    return UBASE_ERR_NONE;
}

static void check_buffer(const uint8_t *buffer, size_t size,
                         size_t *total_size_p)
{
    int i;
    for (i = 0; i < size; i++)
        assert(buffer[i] == (*total_size_p)-- % 256);
}

static void check_ubuf(struct ubuf *ubuf, uint8_t stream_id, bool unitstart,
                       bool randomaccess, bool discontinuity, bool alignment,
                       uint64_t pcr_prog, uint64_t dts_prog, uint64_t pts_prog,
                       size_t *total_size_p, uint64_t *payload_size_p)
{
    assert(ubuf != NULL);
    *payload_size_p = 0;

    /* check header */
    int size = -1;
    const uint8_t *buffer;
    ubase_assert(ubuf_block_read(ubuf, 0, &size, &buffer));
    assert(size >= TS_HEADER_SIZE);
    assert(ts_validate(buffer));
    assert(ts_get_pid(buffer) == 68);
    if (ts_has_payload(buffer)) {
        last_cc++;
        last_cc &= 0xf;
    }
    assert(ts_get_cc(buffer) == last_cc);
    assert(ts_has_payload(buffer) == !!*total_size_p);
    assert(ts_get_unitstart(buffer) == unitstart);

    /* check af */
    if (ts_has_adaptation(buffer))
        assert(size == TS_HEADER_SIZE + 1 + ts_get_adaptation(buffer));
    else
        assert(size == TS_HEADER_SIZE);
    if (randomaccess || discontinuity)
        assert(size >= TS_HEADER_SIZE_AF);
    if (ts_has_adaptation(buffer) && ts_get_adaptation(buffer)) {
        assert(tsaf_has_randomaccess(buffer) == randomaccess);
        assert(tsaf_has_discontinuity(buffer) == discontinuity);
        if (pcr_prog != UINT64_MAX) {
            assert(tsaf_has_pcr(buffer));
            assert(tsaf_get_pcr(buffer) * 300 + tsaf_get_pcrext(buffer) ==
                   pcr_prog);
            pcr_prog = UINT64_MAX;
        } else
            assert(!tsaf_has_pcr(buffer));
    }
    ubuf_block_unmap(ubuf, 0);
    assert(pcr_prog == UINT64_MAX);

    int offset = size;
    if (unitstart) {
        if (!stream_id) {
            /* check pointer_field */
            size = -1;
            ubase_assert(ubuf_block_read(ubuf, offset, &size, &buffer));
            assert(size == 1);
            assert(buffer[0] == 0);
            ubuf_block_unmap(ubuf, offset);
            offset++;
            *total_size_p -= size;
            *payload_size_p += size;
        } else {
            /* check PES header */
            size = -1;
            ubase_assert(ubuf_block_read(ubuf, offset, &size, &buffer));
            assert(size >= PES_HEADER_SIZE);
            assert(pes_validate(buffer));
            assert(pes_get_streamid(buffer) == stream_id);
            uint16_t pes_size = pes_get_length(buffer);
            if (stream_id != PES_STREAM_ID_PRIVATE_2) {
                assert(size >= PES_HEADER_SIZE_NOPTS);
                assert(pes_validate_header(buffer));
                assert(pes_get_dataalignment(buffer) == alignment);
                assert(size == pes_get_headerlength(buffer) +
                               PES_HEADER_SIZE_NOPTS);

                if (pes_has_pts(buffer)) {
                    assert(size >= PES_HEADER_SIZE_PTS);
                    assert(pes_validate_pts(buffer));
                    assert(pts_prog / 300 == pes_get_pts(buffer));
                    pts_prog = UINT64_MAX;
                    if (pes_has_dts(buffer)) {
                        assert(size >= PES_HEADER_SIZE_PTSDTS);
                        assert(pes_validate_dts(buffer));
                        assert(dts_prog / 300 == pes_get_dts(buffer));
                        dts_prog = UINT64_MAX;
                    }
                }
            }
            ubuf_block_unmap(ubuf, 0);
            assert(*total_size_p == pes_size + PES_HEADER_SIZE);
            offset += size;
            *total_size_p -= size;
            *payload_size_p += size;
        }
    }
    assert(pts_prog == UINT64_MAX);
    assert(dts_prog == UINT64_MAX);

    if (offset != TS_SIZE) {
        /* check payload */
        if (!stream_id) {
            size = -1;
            ubase_assert(ubuf_block_read(ubuf, offset, &size, &buffer));
            check_buffer(buffer, size, total_size_p);
            ubuf_block_unmap(ubuf, offset);
            *payload_size_p += size;

            if (size + offset != TS_SIZE) {
                /* check padding */
                offset += size;
                ubase_assert(ubuf_block_read(ubuf, offset, &size, &buffer));
                assert(size + offset == TS_SIZE);
                int i;
                for (i = 0; i < size; i++)
                    assert(buffer[i] == 0xff);
                ubuf_block_unmap(ubuf, offset);
            }
        } else {
            uint8_t copy[TS_SIZE - offset];
            buffer = ubuf_block_peek(ubuf, offset, TS_SIZE - offset, copy);
            assert(buffer != NULL);
            check_buffer(buffer, TS_SIZE - offset, total_size_p);
            ubase_assert(ubuf_block_peek_unmap(ubuf, offset, copy, buffer));
        }
    }
}

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct uref *flow_def;
    flow_def = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(flow_def != NULL);
    ubase_assert(uref_block_flow_set_octetrate(flow_def, 2206));
    ubase_assert(uref_ts_flow_set_tb_rate(flow_def, 4412));
    ubase_assert(uref_ts_flow_set_pid(flow_def, 68));
    ubase_assert(uref_ts_flow_set_pes_id(flow_def, PES_STREAM_ID_VIDEO_MPEG));
    ubase_assert(uref_ts_flow_set_pes_alignment(flow_def));

    struct upipe_mgr *upipe_ts_encaps_mgr = upipe_ts_encaps_mgr_alloc();
    assert(upipe_ts_encaps_mgr != NULL);
    struct upipe *upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, flow_def));
    uref_free(flow_def);
    uint64_t dts_sys;
    assert(next_cr_sys == UINT64_MAX);
    assert(next_dts_sys == UINT64_MAX);
    assert(!next_ready);
    ubase_assert(upipe_ts_mux_set_pcr_interval(upipe_ts_encaps, UCLOCK_FREQ));
    assert(next_cr_sys == UINT64_MAX);

    struct ubuf *ubuf;
    uint8_t *buffer;
    int size;
    size_t total_size = 2206;
    struct uref *uref = uref_block_alloc(uref_mgr, ubuf_mgr, total_size);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == total_size);
    int i;
    for (i = 0; i < total_size; i++)
        buffer[i] = (total_size - i) % 256;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_prog(uref, UCLOCK_FREQ);
    uref_clock_set_cr_sys(uref, UINT32_MAX + UCLOCK_FREQ);
    uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, UCLOCK_FREQ);
    uref_block_set_start(uref);
    uref_flow_set_discontinuity(uref);
    ubase_assert(uref_flow_set_random(uref));
    upipe_input(upipe_ts_encaps, uref, NULL);
    assert(next_cr_sys <= UINT32_MAX);
    assert(next_ready);
    last_cc = 12;
    ubase_assert(upipe_ts_mux_set_cc(upipe_ts_encaps, last_cc));

    total_size += 19; /* PES header */
    unsigned int nb_ts = (total_size + 8 + TS_SIZE - TS_HEADER_SIZE - 1) /
                         (TS_SIZE - TS_HEADER_SIZE);
    uint64_t payload_size;
    for (i = 0; i < nb_ts; i++) {
        uint64_t mux_sys = UINT32_MAX + i * UCLOCK_FREQ / nb_ts;
        if (i == 0) {
            assert(next_cr_sys <= UINT32_MAX);
            assert(next_dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ -
                           (uint64_t)(total_size - 19) * UCLOCK_FREQ / 4412);
            assert(next_pcr_sys <= UINT32_MAX);
        } else {
            assert(next_cr_sys <= UINT32_MAX + UCLOCK_FREQ -
                                  (uint64_t)total_size * UCLOCK_FREQ / 2206);
            assert(next_dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ -
                                   (uint64_t)total_size * UCLOCK_FREQ / 4412);
            assert(next_pcr_sys == UINT32_MAX + UCLOCK_FREQ);
        }

        ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps, mux_sys, &ubuf,
                                            &dts_sys));

        if (i == 0) {
            assert(dts_sys == mux_sys);
            check_ubuf(ubuf, PES_STREAM_ID_VIDEO_MPEG, true, true, true, true,
                       0, 2 * UCLOCK_FREQ, 3 * UCLOCK_FREQ, &total_size,
                       &payload_size);
        } else {
            assert(dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ -
                             (uint64_t)total_size * UCLOCK_FREQ / 4412);
            check_ubuf(ubuf, 0, false, false, false, false,
                       UINT64_MAX, UINT64_MAX, UINT64_MAX,
                       &total_size, &payload_size);
        }
        ubuf_free(ubuf);
    }
    assert(total_size == 0);

    assert(next_cr_sys == UINT64_MAX);
    assert(next_dts_sys == UINT64_MAX);
    assert(next_pcr_sys <= UINT32_MAX + UCLOCK_FREQ);
    ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps,
                UINT32_MAX + UCLOCK_FREQ, &ubuf, &dts_sys));
    assert(dts_sys == UINT32_MAX + UCLOCK_FREQ);
    check_ubuf(ubuf, 0, false, false, false, false,
               UCLOCK_FREQ, UINT64_MAX, UINT64_MAX, &total_size, &payload_size);
    ubuf_free(ubuf);

    upipe_release(upipe_ts_encaps);

    flow_def = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(flow_def != NULL);
    /* This is calculated so that there is no padding. */
    ubase_assert(uref_block_flow_set_octetrate(flow_def, 2194));
    ubase_assert(uref_ts_flow_set_tb_rate(flow_def, 4400));
    ubase_assert(uref_ts_flow_set_pid(flow_def, 68));
    ubase_assert(uref_ts_flow_set_pes_id(flow_def, PES_STREAM_ID_PRIVATE_2));
    ubase_assert(uref_ts_flow_set_pes_alignment(flow_def));

    upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, flow_def));
    uref_free(flow_def);
    ubase_assert(upipe_ts_mux_set_pcr_interval(upipe_ts_encaps, UCLOCK_FREQ));

    total_size = 2194;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, total_size);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == total_size);
    for (i = 0; i < total_size; i++)
        buffer[i] = (total_size - i) % 256;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_prog(uref, UCLOCK_FREQ);
    uref_clock_set_cr_sys(uref, UINT32_MAX + UCLOCK_FREQ);
    uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, UCLOCK_FREQ);
    uref_block_set_start(uref);
    upipe_input(upipe_ts_encaps, uref, NULL);
    last_cc = 3;
    ubase_assert(upipe_ts_mux_set_cc(upipe_ts_encaps, last_cc));

    total_size += 6; /* PES header */
    nb_ts = (total_size + 2 + TS_SIZE - TS_HEADER_SIZE - 1) /
            (TS_SIZE - TS_HEADER_SIZE);
    for (i = 0; i < nb_ts; i++) {
        uint64_t mux_sys = UINT32_MAX + i * UCLOCK_FREQ / (nb_ts + 1);
        if (i == 0) {
            assert(next_cr_sys <= mux_sys);
        } else {
            assert(next_cr_sys <= UINT32_MAX + UCLOCK_FREQ -
                                  (uint64_t)total_size * UCLOCK_FREQ / 2194);
        }

        ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps, mux_sys, &ubuf,
                                            &dts_sys));

        if (i == 0) {
            check_ubuf(ubuf, PES_STREAM_ID_PRIVATE_2, true, false, false, true,
                       0, UINT64_MAX, UINT64_MAX, &total_size, &payload_size);
        } else {
            check_ubuf(ubuf, 0, false, false, false, false,
                       UINT64_MAX, UINT64_MAX, UINT64_MAX,
                       &total_size, &payload_size);
        }
        ubuf_free(ubuf);
    }
    assert(total_size == 0);

    upipe_release(upipe_ts_encaps);

    flow_def = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(flow_def != NULL);
    ubase_assert(uref_block_flow_set_octetrate(flow_def, 2));
    ubase_assert(uref_ts_flow_set_tb_rate(flow_def, 2));
    ubase_assert(uref_ts_flow_set_pid(flow_def, 68));
    ubase_assert(uref_ts_flow_set_pes_id(flow_def, PES_STREAM_ID_AUDIO_MPEG));
    ubase_assert(uref_ts_flow_set_pes_alignment(flow_def));
    ubase_assert(uref_ts_flow_set_pes_min_duration(flow_def, UCLOCK_FREQ));

    upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, flow_def));
    uref_free(flow_def);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1);
    buffer[0] = 2;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_prog(uref, UCLOCK_FREQ);
    uref_clock_set_cr_sys(uref, UINT32_MAX + UCLOCK_FREQ);
    uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_duration(uref, UCLOCK_FREQ / 2);
    upipe_input(upipe_ts_encaps, uref, NULL);
    last_cc = 12;
    ubase_assert(upipe_ts_mux_set_cc(upipe_ts_encaps, last_cc));
    assert(!next_ready);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1);
    buffer[0] = 1;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_prog(uref, UCLOCK_FREQ + UCLOCK_FREQ / 2);
    uref_clock_set_cr_sys(uref, UINT32_MAX + 3 * UCLOCK_FREQ / 2);
    uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_clock_set_duration(uref, UCLOCK_FREQ / 2);
    upipe_input(upipe_ts_encaps, uref, NULL);
    assert(next_ready);
    assert(next_cr_sys <= UINT32_MAX + UCLOCK_FREQ / 2);
    assert(next_dts_sys == UINT32_MAX + 3 * UCLOCK_FREQ / 2);

    ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps,
                UINT32_MAX + UCLOCK_FREQ / 2, &ubuf, &dts_sys));
    assert(dts_sys == UINT32_MAX + 3 * UCLOCK_FREQ / 2);
    total_size = 2 + 14;
    check_ubuf(ubuf, PES_STREAM_ID_AUDIO_MPEG, true, false, false, true,
               UINT64_MAX, UINT64_MAX, UCLOCK_FREQ * 2,
               &total_size, &payload_size);
    ubuf_free(ubuf);

    upipe_release(upipe_ts_encaps);

    flow_def = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(flow_def != NULL);
    ubase_assert(uref_block_flow_set_octetrate(flow_def, 170));
    ubase_assert(uref_ts_flow_set_tb_rate(flow_def, 170));
    ubase_assert(uref_ts_flow_set_pid(flow_def, 68));
    ubase_assert(uref_ts_flow_set_pes_id(flow_def, PES_STREAM_ID_AUDIO_MPEG));

    upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, flow_def));
    uref_free(flow_def);

    total_size = 168;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 169);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 169);
    for (i = 0; i < total_size; i++)
        buffer[i] = (total_size - i) % 256;
    buffer[total_size] = 2;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_prog(uref, UCLOCK_FREQ);
    uref_clock_set_cr_sys(uref, UINT32_MAX + UCLOCK_FREQ);
    uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, 0);
    uref_flow_set_random(uref);
    upipe_input(upipe_ts_encaps, uref, NULL);
    last_cc = 9;
    ubase_assert(upipe_ts_mux_set_cc(upipe_ts_encaps, last_cc));
    assert(!next_ready);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1);
    buffer[0] = 1;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_prog(uref, 2 * UCLOCK_FREQ);
    uref_clock_set_cr_sys(uref, UINT32_MAX + 2 * UCLOCK_FREQ);
    uref_clock_set_cr_dts_delay(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, 0);
    upipe_input(upipe_ts_encaps, uref, NULL);
    assert(next_ready);
    assert(next_cr_sys <= UINT32_MAX + UCLOCK_FREQ - 169 * UCLOCK_FREQ / 170);
    assert(next_dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ - 169 * UCLOCK_FREQ / 170);

    ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps,
                UINT32_MAX + UCLOCK_FREQ - 169 * UCLOCK_FREQ / 170, &ubuf,
                &dts_sys));
    /* rounding issue */
    assert(dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ - 168 * UCLOCK_FREQ / 170 - UCLOCK_FREQ / 170);
    total_size += 14;
    check_ubuf(ubuf, PES_STREAM_ID_AUDIO_MPEG, true, true, false, true,
               UINT64_MAX, UINT64_MAX, UCLOCK_FREQ * 2,
               &total_size, &payload_size);
    ubuf_free(ubuf);

    assert(!next_ready);
    ubase_assert(upipe_ts_encaps_eos(upipe_ts_encaps));
    assert(next_ready);
    assert(next_cr_sys <= UINT32_MAX + UCLOCK_FREQ - UCLOCK_FREQ / 170);
    assert(next_dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ - UCLOCK_FREQ / 170);

    ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps,
                UINT32_MAX + UCLOCK_FREQ - UCLOCK_FREQ / 170, &ubuf,
                &dts_sys));
    assert(dts_sys == UINT32_MAX + 2 * UCLOCK_FREQ - UCLOCK_FREQ / 170);
    total_size = 14 + 2;
    check_ubuf(ubuf, PES_STREAM_ID_AUDIO_MPEG, true, false, false, false,
               UINT64_MAX, UINT64_MAX, UCLOCK_FREQ * 3,
               &total_size, &payload_size);
    ubuf_free(ubuf);

    upipe_release(upipe_ts_encaps);

    flow_def = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.");
    assert(flow_def != NULL);
    /* This is calculated so that there is no padding. */
    ubase_assert(uref_block_flow_set_octetrate(flow_def, 1024));
    ubase_assert(uref_ts_flow_set_tb_rate(flow_def, 2050));
    ubase_assert(uref_ts_flow_set_pid(flow_def, 68));

    upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, flow_def));
    uref_free(flow_def);

    total_size = 1024;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, total_size);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == total_size);
    for (i = 0; i < total_size; i++)
        buffer[i] = (total_size - i) % 256;
    uref_block_unmap(uref, 0);
    uref_clock_set_cr_sys(uref, UINT32_MAX);
    uref_block_set_start(uref);
    upipe_input(upipe_ts_encaps, uref, NULL);
    ubase_assert(upipe_ts_mux_set_cc(upipe_ts_encaps, last_cc));

    total_size += 1; /* pointer_field */
    nb_ts = (total_size + TS_SIZE - TS_HEADER_SIZE - 1) /
            (TS_SIZE - TS_HEADER_SIZE);
    for (i = 0; i < nb_ts; i++) {
        uint64_t mux_sys = UINT32_MAX + i * UCLOCK_FREQ / nb_ts;
        if (i == 0) {
            assert(next_cr_sys <= UINT32_MAX -
                             (uint64_t)(total_size - 1) * UCLOCK_FREQ / 1024);
        } else {
            assert(next_cr_sys <= UINT32_MAX -
                             (uint64_t)total_size * UCLOCK_FREQ / 1024);
        }

        ubase_assert(upipe_ts_encaps_splice(upipe_ts_encaps, mux_sys,
                                            &ubuf, &dts_sys));

        if (i == 0) {
            check_ubuf(ubuf, 0, true, false, false, false,
                       UINT64_MAX, UINT64_MAX, UINT64_MAX, &total_size,
                       &payload_size);
        } else {
            check_ubuf(ubuf, 0, false, false, false, false,
                       UINT64_MAX, UINT64_MAX, UINT64_MAX, &total_size,
                       &payload_size);
        }
        ubuf_free(ubuf);
    }
    assert(total_size == 0);

    upipe_release(upipe_ts_encaps);

    upipe_mgr_release(upipe_ts_encaps_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

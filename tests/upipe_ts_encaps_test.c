/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static size_t total_size = 0;
static uint8_t cc = 0;
static bool randomaccess = true;
static bool discontinuity = true;
static uint64_t dts, dts_sys, dts_step;
static unsigned int nb_ts;
static uint64_t next_pcr = UINT64_MAX;
static uint64_t pcr_tolerance = 0;
static bool psi = false;
static bool psi_first = false;

/** definition of our uprobe */
static enum ubase_err catch(struct uprobe *uprobe, struct upipe *upipe,
                            enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_ts_encaps */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe, uint32_t signature,
                                   va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_ts_encaps */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump **upump_p)
{
    assert(uref != NULL);
    /* check attributes */
    uint64_t uref_dts, uref_dts_sys, vbv_delay = 0;
    ubase_assert(uref_clock_get_dts_prog(uref, &uref_dts));
    ubase_assert(uref_clock_get_dts_sys(uref, &uref_dts_sys));
    uref_clock_get_cr_dts_delay(uref, &vbv_delay);
    uref_dts -= vbv_delay;
    uref_dts_sys -= vbv_delay;
    --nb_ts;
    assert(uref_dts == dts - dts_step * nb_ts);
    assert(uref_dts_sys == dts_sys - dts_step * nb_ts);
    if (next_pcr <= uref_dts + pcr_tolerance)
        ubase_assert(uref_clock_get_ref(uref));

    /* check header */
    cc++;
    cc &= 0xff;
    int size = -1;
    const uint8_t *buffer;
    ubase_assert(uref_block_read(uref, 0, &size, &buffer));
    assert(size >= TS_HEADER_SIZE);
    assert(ts_validate(buffer));
    assert(ts_get_pid(buffer) == 68);
    assert(ts_get_cc(buffer) == cc);
    assert(ts_has_payload(buffer));
    assert(ts_get_unitstart(buffer) == !total_size);

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
        if (next_pcr <= uref_dts + pcr_tolerance) {
            assert(tsaf_has_pcr(buffer));
            next_pcr += UCLOCK_FREQ / 5;
        } else
            assert(!tsaf_has_pcr(buffer));
    }
    uref_block_unmap(uref, 0);

    int offset = size;
    if (psi_first) {
        /* check pointer_field */
        size = -1;
        ubase_assert(uref_block_read(uref, offset, &size, &buffer));
        assert(size == 1);
        assert(buffer[0] == 0);
        uref_block_unmap(uref, offset);
        offset++;
        psi_first = false;
    }

    /* check payload */
    size = -1;
    ubase_assert(uref_block_read(uref, offset, &size, &buffer));
    if (size + offset != TS_SIZE)
        assert(psi);
    int i;
    for (i = 0; i < size; i++)
        assert(buffer[i] == total_size++ % 256);
    uref_block_unmap(uref, offset);

    if (size + offset != TS_SIZE) {
        /* check padding */
        offset += size;
        ubase_assert(uref_block_read(uref, offset, &size, &buffer));
        assert(size + offset == TS_SIZE);
        for (i = 0; i < size; i++)
            assert(buffer[i] == 0xff);
        uref_block_unmap(uref, offset);
        psi = false;
    }
    uref_free(uref);

    randomaccess = false;
    discontinuity = false;
}

/** helper phony pipe to test upipe_ts_encaps */
static void ts_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_encaps */
static struct upipe_mgr ts_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = ts_test_alloc,
    .upipe_input = ts_test_input,
    .upipe_control = NULL
};

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

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr,
                                                uprobe_use(logger));
    assert(upipe_sink != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    ubase_assert(uref_block_flow_set_octetrate(uref, 2206));
    ubase_assert(uref_ts_flow_set_tb_rate(uref, 2206));
    ubase_assert(uref_ts_flow_set_pid(uref, 68));

    struct upipe_mgr *upipe_ts_encaps_mgr = upipe_ts_encaps_mgr_alloc();
    assert(upipe_ts_encaps_mgr != NULL);
    struct upipe *upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_ts_encaps, upipe_sink));

    uint8_t *buffer;
    int size;
    /* this is calculated so that there is no padding */
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 2206);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 2206);
    int i;
    for (i = 0; i < 2206; i++)
        buffer[i] = i % 256;
    uref_block_unmap(uref, 0);
    uref_clock_set_dts_prog(uref, 27000000 + 27000000);
    uref_clock_set_dts_sys(uref, 270000000 + 27000000);
    uref_clock_set_cr_dts_delay(uref, 27000000);
    uref_block_set_start(uref);
    uref_flow_set_discontinuity(uref);
    ubase_assert(uref_flow_set_random(uref));
    dts = 27000000;
    dts_sys = 270000000;
    nb_ts = 2206 / (TS_SIZE - TS_HEADER_SIZE) + 1;
    dts_step = 27000000 / nb_ts;
    upipe_input(upipe_ts_encaps, uref, NULL);
    assert(total_size == 2206);
    assert(nb_ts == 0);
    upipe_release(upipe_ts_encaps);

    total_size = 0;
    cc = 0;
    randomaccess = true;
    discontinuity = true;
    next_pcr = 0;
    pcr_tolerance = (uint64_t)TS_SIZE * UCLOCK_FREQ / 2048;
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    ubase_assert(uref_block_flow_set_octetrate(uref, 2048));
    ubase_assert(uref_ts_flow_set_tb_rate(uref, 2048));
    ubase_assert(uref_ts_flow_set_pid(uref, 68));

    upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, uref));
    uref_free(uref);
    ubase_assert(upipe_ts_mux_set_pcr_interval(upipe_ts_encaps, UCLOCK_FREQ / 5));
    ubase_assert(upipe_set_output(upipe_ts_encaps, upipe_sink));

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 2048);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 2048);
    for (i = 0; i < 2048; i++)
        buffer[i] = i % 256;
    uref_block_unmap(uref, 0);
    uref_clock_set_dts_prog(uref, 27000000);
    uref_clock_set_dts_sys(uref, 270000000);
    uref_block_set_start(uref);
    uref_flow_set_discontinuity(uref);
    ubase_assert(uref_flow_set_random(uref));
    dts = 27000000;
    dts_sys = 270000000;
    nb_ts = 2048 / (TS_SIZE - TS_HEADER_SIZE) + 1;
    dts_step = 27000000 / nb_ts;
    upipe_input(upipe_ts_encaps, uref, NULL);
    assert(total_size == 2048);
    assert(nb_ts == 0);
    upipe_release(upipe_ts_encaps);

    total_size = 0;
    cc = 0;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.");
    assert(uref != NULL);
    ubase_assert(uref_block_flow_set_octetrate(uref, 1025));
    ubase_assert(uref_ts_flow_set_tb_rate(uref, 1025));
    ubase_assert(uref_ts_flow_set_pid(uref, 68));

    upipe_ts_encaps = upipe_void_alloc(upipe_ts_encaps_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts encaps"));
    assert(upipe_ts_encaps != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_encaps, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_ts_encaps, upipe_sink));

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1024);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 1024);
    for (i = 0; i < 1024; i++)
        buffer[i] = i % 256;
    uref_block_unmap(uref, 0);
    uref_clock_set_dts_prog(uref, 27000000);
    uref_clock_set_dts_sys(uref, 270000000);
    dts = 27000000;
    dts_sys = 270000000;
    nb_ts = 1024 / (TS_SIZE - TS_HEADER_SIZE) + 1;
    dts_step = 27000000 / nb_ts;
    psi = true;
    psi_first = true;
    uref_block_set_start(uref);
    upipe_input(upipe_ts_encaps, uref, NULL);
    assert(total_size == 1024);
    assert(nb_ts == 0);
    upipe_release(upipe_ts_encaps);

    upipe_mgr_release(upipe_ts_encaps_mgr); // nop

    ts_test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

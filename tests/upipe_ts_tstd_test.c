/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short unit tests for module calculating the T-STD buffering latency
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_tstd.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

static uint64_t cr_dts_delay = UINT64_MAX;

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
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
        case UPROBE_NEW_FLOW_DEF:
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe to test upipe_ts_tstd */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_ts_tstd */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    ubase_assert(uref_clock_get_cr_dts_delay(uref, &cr_dts_delay));
    upipe_dbg_va(upipe, "delay: %"PRIu64, cr_dts_delay);
    uref_free(uref);
}

/** helper phony pipe to test upipe_ts_tstd */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_tstd */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
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
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpeg2video.pic.");
    assert(uref != NULL);
    ubase_assert(uref_block_flow_set_octetrate(uref, 100));
    ubase_assert(uref_block_flow_set_buffer_size(uref, 100));
    const struct urational fps = { .num = 10, .den = 1 };
    ubase_assert(uref_pic_flow_set_fps(uref, fps));

    struct upipe *upipe_sink = upipe_void_alloc(&test_mgr,
                                                uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_ts_tstd_mgr = upipe_ts_tstd_mgr_alloc();
    assert(upipe_ts_tstd_mgr != NULL);
    struct upipe *upipe_ts_tstd = upipe_void_alloc(upipe_ts_tstd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "tstd"));
    assert(upipe_ts_tstd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_tstd, uref));
    ubase_assert(upipe_set_output(upipe_ts_tstd, upipe_sink));
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 10);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ - UCLOCK_FREQ / 10);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 50);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    cr_dts_delay = UINT64_MAX;
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ / 2);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 10);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    cr_dts_delay = UINT64_MAX;
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ / 2);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 50);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    cr_dts_delay = UINT64_MAX;
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ / 10);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 10);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    cr_dts_delay = UINT64_MAX;
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ / 10);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 5);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    cr_dts_delay = UINT64_MAX;
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ / 10 + UCLOCK_FREQ / 20);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 5);
    assert(uref != NULL);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ / 10));
    cr_dts_delay = UINT64_MAX;
    upipe_input(upipe_ts_tstd, uref, NULL);
    assert(cr_dts_delay == UCLOCK_FREQ / 5);

    upipe_release(upipe_ts_tstd);
    upipe_mgr_release(upipe_ts_tstd_mgr);

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}

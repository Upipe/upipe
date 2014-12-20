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
 * @short unit tests for PES encaps module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
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
#include <upipe-ts/upipe_ts_pes_encaps.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/pes.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static uint8_t stream_id = PES_STREAM_ID_VIDEO_MPEG;
static ssize_t total_size = 0;
static size_t header_size = 0;
static uint16_t pes_size = 0;

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
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    uint64_t pts = UINT64_MAX, dts = UINT64_MAX;
    uref_clock_get_pts_prog(uref, &pts);
    uref_clock_get_dts_prog(uref, &dts);

    if (!total_size) {
        /* check header */
        int size = -1;
        const uint8_t *buffer;
        ubase_assert(uref_block_read(uref, 0, &size, &buffer));
        header_size = size;
        assert(size >= PES_HEADER_SIZE);
        assert(pes_validate(buffer));
        assert(pes_get_streamid(buffer) == stream_id);
        pes_size = pes_get_length(buffer);
        if (stream_id != PES_STREAM_ID_PRIVATE_2) {
            assert(size >= PES_HEADER_SIZE_NOPTS);
            assert(pes_validate_header(buffer));
            assert(pes_get_dataalignment(buffer));
            assert(size == pes_get_headerlength(buffer) + PES_HEADER_SIZE_NOPTS);

            if (pes_has_pts(buffer)) {
                assert(size >= PES_HEADER_SIZE_PTS);
                assert(pes_validate_pts(buffer));
                assert(pts / 300 == pes_get_pts(buffer));
                if (pes_has_dts(buffer)) {
                    assert(size >= PES_HEADER_SIZE_PTSDTS);
                    assert(pes_validate_dts(buffer));
                    assert(dts / 300 == pes_get_dts(buffer));
                }
            }
        }
        uref_block_unmap(uref, 0);
        total_size -= size;
    }

    /* check payload */
    size_t uref_size;
    ubase_assert(uref_block_size(uref, &uref_size));
    total_size += uref_size;
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr ts_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
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
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr,
                                                uprobe_use(logger));
    assert(upipe_sink != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    ubase_assert(uref_ts_flow_set_pes_id(uref, PES_STREAM_ID_VIDEO_MPEG));

    struct upipe_mgr *upipe_ts_pese_mgr = upipe_ts_pese_mgr_alloc();
    assert(upipe_ts_pese_mgr != NULL);
    struct upipe *upipe_ts_pese = upipe_void_alloc(upipe_ts_pese_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts pese"));
    assert(upipe_ts_pese != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_pese, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_ts_pese, upipe_sink));

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 2048);
    assert(uref != NULL);
    uref_clock_set_dts_prog(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, UCLOCK_FREQ);
    upipe_input(upipe_ts_pese, uref, NULL);
    assert(total_size == 2048);
    assert(header_size == PES_HEADER_SIZE_PTSDTS);

    total_size = 0;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 70000);
    assert(uref != NULL);
    uref_clock_set_dts_prog(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, 0);
    upipe_input(upipe_ts_pese, uref, NULL);
    assert(total_size == 70000);
    assert(header_size == PES_HEADER_SIZE_PTS);

    upipe_release(upipe_ts_pese);
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    stream_id = PES_STREAM_ID_PRIVATE_1;
    ubase_assert(uref_ts_flow_set_pes_id(uref, stream_id));
    ubase_assert(uref_ts_flow_set_pes_header(uref, 45));
    upipe_ts_pese = upipe_void_alloc(upipe_ts_pese_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts pese"));
    assert(upipe_ts_pese != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_pese, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_ts_pese, upipe_sink));

    total_size = 0;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 1);
    assert(uref != NULL);
    upipe_input(upipe_ts_pese, uref, NULL);
    assert(total_size == 1);
    assert(header_size == 45);

    upipe_release(upipe_ts_pese);
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    stream_id = PES_STREAM_ID_PRIVATE_2;
    ubase_assert(uref_ts_flow_set_pes_id(uref, stream_id));
    upipe_ts_pese = upipe_void_alloc(upipe_ts_pese_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts pese"));
    assert(upipe_ts_pese != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_pese, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_ts_pese, upipe_sink));

    total_size = 0;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 12);
    assert(uref != NULL);
    upipe_input(upipe_ts_pese, uref, NULL);
    assert(total_size == 12);
    assert(header_size == PES_HEADER_SIZE);

    upipe_release(upipe_ts_pese);
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    stream_id = PES_STREAM_ID_AUDIO_MPEG;
    ubase_assert(uref_ts_flow_set_pes_id(uref, stream_id));
    ubase_assert(uref_ts_flow_set_pes_min_duration(uref, UCLOCK_FREQ * 2));
    upipe_ts_pese = upipe_void_alloc(upipe_ts_pese_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                   "ts pese"));
    assert(upipe_ts_pese != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_pese, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_ts_pese, upipe_sink));

    total_size = 0;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 12);
    assert(uref != NULL);
    uref_clock_set_dts_prog(uref, UCLOCK_FREQ);
    uref_clock_set_dts_pts_delay(uref, 0);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ));
    upipe_input(upipe_ts_pese, uref, NULL);
    assert(total_size == 0);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 12);
    assert(uref != NULL);
    uref_clock_set_dts_prog(uref, UCLOCK_FREQ * 2);
    uref_clock_set_dts_pts_delay(uref, 0);
    ubase_assert(uref_clock_set_duration(uref, UCLOCK_FREQ));
    upipe_input(upipe_ts_pese, uref, NULL);
    assert(total_size == 24);

    upipe_release(upipe_ts_pese);
    upipe_mgr_release(upipe_ts_pese_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);

    return 0;
}

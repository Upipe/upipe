/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS pesd module
 */

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_std.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
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
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_pesd.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/pes.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define ULOG_LEVEL ULOG_DEBUG

static unsigned int nb_packets = 0;
static uint64_t pts = 0x112121212;
static uint64_t dts = 0x112121212 - 1080000;
static bool dataalignment = true;
static size_t payload_size = 12;
static bool expect_lost = false;
static bool expect_acquired = true;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_AERROR:
        case UPROBE_UPUMP_ERROR:
        case UPROBE_READ_END:
        case UPROBE_WRITE_END:
        case UPROBE_NEW_FLOW:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_LINEAR_NEED_UBUF_MGR:
        case UPROBE_SOURCE_NEED_FLOW_NAME:
        default:
            assert(0);
            break;
        case UPROBE_READY:
            break;
        case UPROBE_SYNC_ACQUIRED:
            fprintf(stdout, "ts probe: pipe %p acquired PES sync\n", upipe);
            assert(expect_acquired);
            expect_acquired = false;
            break;
        case UPROBE_SYNC_LOST:
            fprintf(stdout, "ts probe: pipe %p lost PES sync\n", upipe);
            assert(expect_lost);
            expect_lost = false;
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_ts_pesd */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe, struct ulog *ulog)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_init(upipe, uprobe, ulog);
    upipe->mgr = mgr;
    return upipe;
}

/** helper phony pipe to test upipe_ts_pesd */
static bool ts_test_control(struct upipe *upipe, enum upipe_command command,
                            va_list args)
{
    if (likely(command == UPIPE_INPUT)) {
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        if (uref_flow_get_delete(uref)) {
            uref_free(uref);
            return true;
        }

        const char *def;
        if (uref_flow_get_def(uref, &def)) {
            uref_free(uref);
            return true;
        }

        size_t size;
        assert(uref_block_size(uref, &size));
        assert(size == payload_size);
        assert(dataalignment == uref_block_get_start(uref));
        uint64_t uref_pts = 0, uref_dts = 0;
        uref_clock_get_pts_orig(uref, &uref_pts);
        assert(uref_pts == pts);
        uref_clock_get_dtsdelay(uref, &uref_dts);
        uref_dts = uref_pts - uref_dts;
        assert(uref_dts == dts);
        uref_free(uref);
        nb_packets--;
        return true;
    }
    return false;
}

/** helper phony pipe to test upipe_ts_pesd */
static void ts_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_pesd */
static struct upipe_mgr ts_test_mgr = {
    .upipe_alloc = ts_test_alloc,
    .upipe_control = ts_test_control,
    .upipe_use = NULL,
    .upipe_release = NULL,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
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
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_print = uprobe_print_alloc(&uprobe, stdout, "test");
    assert(uprobe_print != NULL);

    struct upipe *upipe_sink = upipe_alloc(&ts_test_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "sink"));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_ts_pesd_mgr = upipe_ts_pesd_mgr_alloc();
    assert(upipe_ts_pesd_mgr != NULL);
    struct upipe *upipe_ts_pesd = upipe_alloc(upipe_ts_pesd_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "ts pesd"));
    assert(upipe_ts_pesd != NULL);
    assert(upipe_linear_set_output(upipe_ts_pesd, upipe_sink));

    struct uref *uref;
    uint8_t *buffer;
    int size;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspes.");
    assert(uref != NULL);
    assert(uref_flow_set_name(uref, "source"));
    assert(upipe_input(upipe_ts_pesd, uref));

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PES_HEADER_SIZE_PTSDTS + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PES_HEADER_SIZE_PTSDTS + 12);
    pes_init(buffer);
    pes_set_streamid(buffer, PES_STREAM_ID_VIDEO_MPEG);
    pes_set_length(buffer, PES_HEADER_SIZE_PTSDTS + 12 - PES_HEADER_SIZE);
    pes_set_headerlength(buffer, PES_HEADER_SIZE_PTSDTS - PES_HEADER_SIZE_NOPTS);
    pes_set_dataalignment(buffer);
    pes_set_pts(buffer, pts);
    pes_set_dts(buffer, dts);
    uref_block_unmap(uref, 0, size);
    assert(uref_flow_set_name(uref, "source"));
    assert(uref_block_set_start(uref));
    nb_packets++;
    assert(upipe_input(upipe_ts_pesd, uref));
    assert(!nb_packets);
    assert(!expect_acquired);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PES_HEADER_SIZE_PTS);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PES_HEADER_SIZE_PTS);
    pes_init(buffer);
    pes_set_streamid(buffer, PES_STREAM_ID_VIDEO_MPEG);
    pes_set_length(buffer, PES_HEADER_SIZE_PTS - PES_HEADER_SIZE);
    pes_set_headerlength(buffer, PES_HEADER_SIZE_PTS - PES_HEADER_SIZE_NOPTS);
    dataalignment = false;
    pes_set_pts(buffer, pts);
    dts = pts;
    uref_block_unmap(uref, 0, size);
    assert(uref_flow_set_name(uref, "source"));
    payload_size = 0;

    /* now cut it into pieces */
    nb_packets++;
    for (int i = 0; i < PES_HEADER_SIZE_PTS; i++) {
        struct uref *dup = uref_dup(uref);
        assert(dup != NULL);
        assert(uref_block_resize(dup, i, 1));
        if (!i)
            assert(uref_block_set_start(dup));
        assert(upipe_input(upipe_ts_pesd, dup));
    }
    assert(!nb_packets);
    uref_free(uref);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42);
    assert(uref != NULL);
    payload_size = 42;
    dataalignment = false;
    pts = dts = 0;
    assert(uref_flow_set_name(uref, "source"));
    nb_packets++;
    assert(upipe_input(upipe_ts_pesd, uref));
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PES_HEADER_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PES_HEADER_SIZE);
    pes_init(buffer);
    pes_set_streamid(buffer, PES_STREAM_ID_PADDING);
    pes_set_length(buffer, 42);
    uref_block_unmap(uref, 0, size);
    payload_size = 0;
    expect_lost = true;
    assert(uref_flow_set_name(uref, "source"));
    assert(uref_block_set_start(uref));
    /* do not increment nb_packets */
    assert(upipe_input(upipe_ts_pesd, uref));
    assert(!nb_packets);
    assert(!expect_lost);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42);
    assert(uref != NULL);
    payload_size = 42;
    dataalignment = false;
    pts = dts = 0;
    assert(uref_flow_set_name(uref, "source"));
    /* do not increment nb_packets */
    assert(upipe_input(upipe_ts_pesd, uref));
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, PES_HEADER_SIZE_NOPTS + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == PES_HEADER_SIZE_NOPTS + 12);
    pes_init(buffer);
    pes_set_streamid(buffer, PES_STREAM_ID_VIDEO_MPEG);
    pes_set_length(buffer, PES_HEADER_SIZE_NOPTS + 12 - PES_HEADER_SIZE);
    pes_set_headerlength(buffer, 0);
    dataalignment = false;
    uref_block_unmap(uref, 0, size);
    assert(uref_flow_set_name(uref, "source"));
    assert(uref_block_set_start(uref));
    payload_size = 12;
    expect_acquired = true;
    nb_packets++;
    assert(upipe_input(upipe_ts_pesd, uref));
    assert(!nb_packets);
    assert(!expect_acquired);

    upipe_release(upipe_ts_pesd);
    upipe_mgr_release(upipe_ts_pesd_mgr); // nop

    ts_test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_print_free(uprobe_print);

    return 0;
}

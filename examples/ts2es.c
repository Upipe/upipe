/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio_color.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_auto_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_h265_framer.h>

#include <ev.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10

static struct upipe *upipe_sink = NULL;

static int catch_video(struct uprobe *uprobe, struct upipe *upipe,
                       int event, va_list args)
{
    switch (event) {
    case UPROBE_NEED_OUTPUT:
        return upipe_set_output(upipe, upipe_sink);
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: %s <input> <output file>\n", argv[0]);
        exit(-1);
    }

    const char *input = argv[1];
    const char *output = argv[2];

    /* structures managers */
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    udict_mgr_release(udict_mgr);

    /* probes */
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_color_alloc(NULL, stderr, UPROBE_LOG_DEBUG);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(uprobe != NULL);
    uref_mgr_release(uref_mgr);
    upump_mgr_release(upump_mgr);
    umem_mgr_release(umem_mgr);

    struct uprobe uprobe_video;
    uprobe_init(&uprobe_video, catch_video, uprobe_use(uprobe));

    /* pipes */
    struct upipe_mgr *upipe_auto_src_mgr = upipe_auto_src_mgr_alloc();
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "file", upipe_fsrc_mgr);
    upipe_mgr_release(upipe_fsrc_mgr);
    struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
    upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "http", upipe_http_src_mgr);
    upipe_mgr_release(upipe_http_src_mgr);
    struct upipe *upipe_src = upipe_void_alloc(
        upipe_auto_src_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "src"));
    upipe_mgr_release(upipe_auto_src_mgr);
    if (!ubase_check(upipe_set_uri(upipe_src, input))) {
        fprintf(stderr, "invalid input\n");
        exit(EXIT_FAILURE);
    }

    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    upipe_sink = upipe_void_alloc(
        upipe_fsink_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "sink"));
    upipe_mgr_release(upipe_fsink_mgr);
    if (!ubase_check(upipe_fsink_set_path(
                upipe_sink, output, UPIPE_FSINK_CREATE))) {
        fprintf(stderr, "invalid output\n");
        exit(EXIT_FAILURE);
    }

    struct uref *flow_def;
    upipe_get_flow_def(upipe_src, &flow_def);

    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    upipe_ts_demux_mgr_set_mpgvf_mgr(upipe_ts_demux_mgr, upipe_mpgvf_mgr);
    upipe_mgr_release(upipe_mpgvf_mgr);
    struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
    upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr, upipe_h264f_mgr);
    upipe_mgr_release(upipe_h264f_mgr);
    struct upipe_mgr *upipe_h265f_mgr = upipe_h265f_mgr_alloc();
    upipe_ts_demux_mgr_set_h265f_mgr(upipe_ts_demux_mgr, upipe_h265f_mgr);
    upipe_mgr_release(upipe_h265f_mgr);
    struct upipe *ts_demux = upipe_void_alloc_output(
        upipe_src, upipe_ts_demux_mgr,
        uprobe_pfx_alloc(
            uprobe_selflow_alloc(
                uprobe_use(uprobe),
                uprobe_selflow_alloc(
                    uprobe_use(uprobe),
                    uprobe_use(&uprobe_video),
                    UPROBE_SELFLOW_PIC, "auto"),
                UPROBE_SELFLOW_VOID, "auto"),
            UPROBE_LOG_DEBUG, "ts demux"));
    upipe_release(ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);

    /* main loop */
    ev_loop(loop, 0);

    upipe_release(upipe_sink);
    upipe_release(upipe_src);

    uprobe_release(uprobe);
    uprobe_clean(&uprobe_video);

    ev_default_destroy();

    return 0;
}


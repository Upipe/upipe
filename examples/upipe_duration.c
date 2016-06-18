/*
 * Copyright (C) 2013-2016 OpenHeadend S.A.R.L.
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
 *
 */

/** @file
 * @short prints the duration of a TS file, derived from the number of pics
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
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
#include <upipe-modules/upipe_file_source.h>
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

static uint64_t duration = 0;
static struct upipe *sink;

/** helper phony pipe to count pictures */
static struct upipe *count_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                 uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to count pictures */
static void count_input(struct upipe *upipe, struct uref *uref,
                        struct upump **upump_p)
{
    uint64_t uref_duration;
    if (ubase_check(uref_clock_get_duration(uref, &uref_duration)))
        duration += uref_duration;
    uref_free(uref);
}

/** helper phony pipe to count pictures */
static int count_control(struct upipe *upipe, int command, va_list args)
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

/** helper phony pipe to count pictures */
static void count_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to count pictures */
static struct upipe_mgr count_mgr = {
    .refcount = NULL,
    .upipe_alloc = count_alloc,
    .upipe_input = count_input,
    .upipe_control = count_control
};

/** catch callback (demux subpipes for flows) */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    if (event != UPROBE_NEW_FLOW_DEF)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct uref *flow_def = va_arg(args, struct uref *);
    uref_dump(flow_def, upipe->uprobe);
    upipe_set_output(upipe, sink);
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        exit(-1);
    }

    const char *file = argv[1];

    /* structures managers */
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);

    /* probes */
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_alloc(NULL, stderr, UPROBE_LOG_DEBUG);
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

    struct uprobe uprobe_split_s;
    uprobe_init(&uprobe_split_s, catch, uprobe_use(uprobe));

    /* pipes */
    sink = upipe_void_alloc(&count_mgr, NULL);
    assert(sink != NULL);

    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    struct upipe *upipe_src = upipe_void_alloc(upipe_fsrc_mgr,
                   uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG,
                                    "fsrc"));
    upipe_mgr_release(upipe_fsrc_mgr);
    if (!ubase_check(upipe_set_uri(upipe_src, file))) {
        fprintf(stderr, "invalid file\n");
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
    struct upipe *ts_demux = upipe_void_alloc_output(upipe_src,
            upipe_ts_demux_mgr,
            uprobe_pfx_alloc(
                uprobe_selflow_alloc(uprobe_use(uprobe),
                    uprobe_selflow_alloc(uprobe_use(uprobe),
                        uprobe_use(&uprobe_split_s),
                        UPROBE_SELFLOW_PIC, "auto"),
                    UPROBE_SELFLOW_VOID, "auto"),
                UPROBE_LOG_DEBUG, "ts demux"));
    upipe_release(ts_demux);
    upipe_mgr_release(upipe_ts_demux_mgr);

    /* main loop */
    ev_loop(loop, 0);

    upipe_release(upipe_src);
    count_free(sink);

    uprobe_release(uprobe);
    uprobe_clean(&uprobe_split_s);

    ev_default_destroy();
    printf("%.2f\n", (double)duration / UCLOCK_FREQ);

    return 0;
}


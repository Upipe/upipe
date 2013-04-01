/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_select_programs.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_mp2v_framer.h>
#include <upipe-framers/upipe_h264_framer.h>

#include <ev.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50

static struct urational fps = { .num = 0, .den = 0 };
static unsigned int nb_pics = 0;
struct upipe *output = NULL;

/** helper phony pipe to count pictures */
static struct upipe *count_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to count pictures */
static void count_input(struct upipe *upipe, struct uref *uref,
                        struct upump *upump)
{
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        if (!uref_pic_flow_get_fps(uref, &fps))
            fps = (struct urational){ .num = 25, .den = 1 };
        uref_free(uref);
        return;
    }
    if (uref_flow_get_end(uref)) {
        uref_free(uref);
        return;
    }

    nb_pics++;
    uref_free(uref);
}

/** helper phony pipe to count pictures */
static void count_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to count pictures */
static struct upipe_mgr count_mgr = {
    .upipe_alloc = count_alloc,
    .upipe_input = count_input,
    .upipe_control = NULL,
    .upipe_free = count_free,

    .upipe_mgr_free = NULL
};

/** catch callback (main thread) */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_READ_END:
            return true;

        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            struct uref *flow_def = va_arg(args, struct uref *);

            assert(output == NULL);
            output = upipe_alloc_output(upipe,
                     uprobe_pfx_adhoc_alloc(uprobe, UPROBE_LOG_DEBUG, "video"));
            assert(output != NULL);
            upipe_set_flow_def(output, flow_def);

            struct upipe *sink = upipe_alloc(&count_mgr, NULL);
            assert(sink != NULL);
            upipe_set_output(output, sink);
            upipe_release(sink);
            return true;
        }
        default:
            return false;
    }
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
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);
    struct ubuf_mgr *block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                         UBUF_SHARED_POOL_DEPTH, umem_mgr,
                                         -1, -1, -1, 0);
    umem_mgr_release(umem_mgr);

    /* probes */
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_alloc(NULL, stderr, UPROBE_LOG_DEBUG);
    uprobe = uprobe_log_alloc(uprobe, UPROBE_LOG_DEBUG);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    uref_mgr_release(uref_mgr);
    upump_mgr_release(upump_mgr);

    struct uprobe uprobe_split_s;
    uprobe_init(&uprobe_split_s, catch, uprobe);
    struct uprobe *uprobe_split = &uprobe_split_s;
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_PIC, "auto");
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_SOUND, "");
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_SUBPIC, "");
    uprobe_split = uprobe_selprog_alloc(uprobe_split, "auto");

    /* pipes */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    struct upipe *upipe_src = upipe_alloc(upipe_fsrc_mgr,
                   uprobe_pfx_adhoc_alloc(uprobe, UPROBE_LOG_DEBUG, "fsrc"));
    upipe_mgr_release(upipe_fsrc_mgr);
    upipe_set_ubuf_mgr(upipe_src, block_mgr);
    ubuf_mgr_release(block_mgr);
    if (!upipe_fsrc_set_path(upipe_src, file)) {
        fprintf(stderr, "invalid file\n");
        exit(EXIT_FAILURE);
    }

    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    struct upipe_mgr *upipe_mp2vf_mgr = upipe_mp2vf_mgr_alloc();
    upipe_ts_demux_mgr_set_mp2vf_mgr(upipe_ts_demux_mgr, upipe_mp2vf_mgr);
    upipe_mgr_release(upipe_mp2vf_mgr);
    struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
    upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr, upipe_h264f_mgr);
    upipe_mgr_release(upipe_h264f_mgr);
    struct upipe *ts_demux = upipe_alloc(upipe_ts_demux_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_split, UPROBE_LOG_DEBUG, "ts demux"));
    upipe_set_output(upipe_src, ts_demux);
    upipe_release(ts_demux);

    /* main loop */
    ev_loop(loop, 0);

    if (output != NULL)
        upipe_release(output);
    upipe_release(upipe_src);

    uprobe_selprog_set(uprobe_split, "");
    uprobe_split = uprobe_selprog_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe = uprobe_upump_mgr_free(uprobe);
    uprobe = uprobe_uref_mgr_free(uprobe);
    uprobe = uprobe_log_free(uprobe);
    uprobe_stdio_free(uprobe);

    ev_default_destroy();
    if (fps.num) {
        double duration = (double)nb_pics * fps.den / fps.num;
        printf("%.2f\n", duration);
    }

    return 0;
}


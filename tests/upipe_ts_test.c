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
 * @short unit tests for TS demux and mux modules
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_program_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-framers/upipe_a52_framer.h>
#include <upipe-framers/upipe_video_trim.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_noclock.h>
#include <upipe-modules/upipe_even.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define FRAME_QUEUE_LENGTH 255
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static const char *src_file, *sink_file;
static struct uref_mgr *uref_mgr;
static struct upump_mgr *upump_mgr;

static struct upipe_mgr *upipe_noclock_mgr;
static struct upipe_mgr *upipe_vtrim_mgr;
static struct upipe *upipe_even;

static struct uprobe *logger;
static struct uprobe uprobe_src_s;
static struct uprobe uprobe_demux_output_s;
static struct uprobe uprobe_demux_program_s;

/** generic probe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
        case UPROBE_TS_SPLIT_ADD_PID:
        case UPROBE_TS_SPLIT_DEL_PID:
        case UPROBE_SOURCE_END:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_TS_MUX_LAST_CC: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_MUX_SIGNATURE)
            upipe_notice_va(upipe, "last continuity counter: %u",
                            va_arg(args, unsigned int));
            break;
        }
    }
    return UBASE_ERR_NONE;
}

/** probe to catch events from the TS demux outputs */
static int catch_ts_demux_output(struct uprobe *uprobe,
                                 struct upipe *upipe,
                                 int event, va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        upipe_release(upipe);
        return UBASE_ERR_NONE;
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

/** probe to catch events from the TS demux programs */
static int catch_ts_demux_program(struct uprobe *uprobe,
                                  struct upipe *upipe,
                                  int event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END: {
            struct upipe *upipe_ts_mux_program;
            ubase_assert(upipe_get_output(upipe, &upipe_ts_mux_program));
            ubase_assert(upipe_ts_mux_freeze_psi(upipe_ts_mux_program));

            struct upipe *upipe_ts_mux;
            ubase_assert(upipe_sub_get_super(upipe_ts_mux_program,
                                             &upipe_ts_mux));
            ubase_assert(upipe_ts_mux_freeze_psi(upipe_ts_mux));

            upipe_release(upipe);
            return UBASE_ERR_NONE;
        }

        case UPROBE_SPLIT_UPDATE: {
            struct uref *flow_def = NULL;
            while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
                   flow_def != NULL) {
                uint64_t flow_id;
                ubase_assert(uref_flow_get_id(flow_def, &flow_id));

                struct upipe *output = NULL;
                bool found = false;
                while (ubase_check(upipe_iterate_sub(upipe, &output)) &&
                       output != NULL) {
                    struct uref *flow_def2;
                    uint64_t id2;
                    if (ubase_check(upipe_get_flow_def(output, &flow_def2)) &&
                        ubase_check(uref_flow_get_id(flow_def2, &id2)) &&
                        flow_id == id2) {
                        /* We already have an output. */
                        found = true;
                        break;
                    }
                }
                if (found)
                    continue;
                const char *def;
                ubase_assert(uref_flow_get_def(flow_def, &def));
                upipe_notice_va(upipe, "add flow %"PRIu64" (%s)", flow_id, def);

                output = upipe_flow_alloc_sub(upipe,
                    uprobe_pfx_alloc_va(&uprobe_demux_output_s,
                                        UPROBE_LOG_LEVEL,
                                        "ts demux output %"PRIu64,
                                        flow_id), flow_def);
                assert(output != NULL);
                output = upipe_void_alloc_output(output, upipe_noclock_mgr,
                    uprobe_pfx_alloc_va(uprobe_use(logger),
                                        UPROBE_LOG_LEVEL,
                                        "noclock %"PRIu64, flow_id));
                assert(output != NULL);
                if (strstr(def, ".pic.") != NULL) {
                    output = upipe_void_chain_output(output, upipe_vtrim_mgr,
                        uprobe_pfx_alloc_va(uprobe_use(logger),
                                            UPROBE_LOG_LEVEL,
                                            "vtrim %"PRIu64, flow_id));
                    assert(output != NULL);
                }
                output = upipe_void_chain_output_sub(output, upipe_even,
                    uprobe_pfx_alloc_va(uprobe_use(logger),
                                        UPROBE_LOG_LEVEL,
                                        "even %"PRIu64, flow_id));
                assert(output != NULL);

                struct upipe *upipe_ts_mux_program;
                ubase_assert(upipe_get_output(upipe, &upipe_ts_mux_program));
                output = upipe_void_chain_output_sub(output,
                        upipe_ts_mux_program,
                        uprobe_pfx_alloc_va(uprobe_use(logger),
                                            UPROBE_LOG_LEVEL,
                                            "mux input %"PRIu64, flow_id));
                assert(output != NULL);
                upipe_release(output);
            }
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

/** probe to catch events from the TS demux */
static int catch_ts_demux(struct uprobe *uprobe, struct upipe *upipe,
                          int event, va_list args)
{
    switch (event) {
        case UPROBE_SPLIT_UPDATE: {
            struct uref *flow_def = NULL;
            while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
                   flow_def != NULL) {
                uint64_t flow_id;
                ubase_assert(uref_flow_get_id(flow_def, &flow_id));

                struct upipe *program = NULL;
                bool found = false;
                while (ubase_check(upipe_iterate_sub(upipe, &program)) &&
                       program != NULL) {
                    struct uref *flow_def2;
                    uint64_t id2;
                    if (ubase_check(upipe_get_flow_def(program, &flow_def2)) &&
                        ubase_check(uref_flow_get_id(flow_def2, &id2)) &&
                        flow_id == id2) {
                        /* We already have a program */
                        found = true;
                        break;
                    }
                }
                if (found)
                    continue;

                program = upipe_flow_alloc_sub(upipe,
                    uprobe_pfx_alloc_va(&uprobe_demux_program_s,
                                        UPROBE_LOG_LEVEL,
                                        "ts demux program %"PRIu64,
                                        flow_id), flow_def);
                assert(program != NULL);

                struct upipe *upipe_ts_mux;
                ubase_assert(upipe_get_output(upipe, &upipe_ts_mux));
                assert(upipe_ts_mux != NULL);

                program = upipe_void_alloc_output_sub(program,
                        upipe_ts_mux,
                        uprobe_pfx_alloc_va(uprobe_use(logger),
                                            UPROBE_LOG_LEVEL,
                                            "ts mux program %"PRIu64, flow_id));
                assert(program != NULL);
                ubase_assert(upipe_ts_mux_set_version(program, 1));
                upipe_release(program);
            }
            return UBASE_ERR_NONE;
        }
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);
    }
}

static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        upipe_dbg(upipe, "caught source end, dying");
        upipe_release(upipe);
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s <source file> <sink file>\n", argv0);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc != 3)
        usage(argv[0]);
    src_file = argv[1];
    sink_file = argv[2];

    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct uprobe uprobe_s;
    uprobe_init(&uprobe_s, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe_s, stdout, UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    upipe_noclock_mgr = upipe_noclock_mgr_alloc();
    assert(upipe_noclock_mgr != NULL);
    upipe_vtrim_mgr = upipe_vtrim_mgr_alloc();
    assert(upipe_vtrim_mgr != NULL);

    struct upipe_mgr *upipe_even_mgr = upipe_even_mgr_alloc();
    assert(upipe_even_mgr != NULL);
    upipe_even = upipe_void_alloc(upipe_even_mgr,
            uprobe_pfx_alloc(uprobe_use(logger),
                             UPROBE_LOG_LEVEL, "even"));
    assert(upipe_even != NULL);
    upipe_mgr_release(upipe_even_mgr);

    /* file source */
    uprobe_init(&uprobe_src_s, catch_src, uprobe_use(logger));
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    assert(upipe_fsrc_mgr != NULL);
    struct upipe *upipe_fsrc = upipe_void_alloc(upipe_fsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(&uprobe_src_s),
                             UPROBE_LOG_LEVEL, "file source"));
    assert(upipe_fsrc != NULL);
    ubase_assert(upipe_set_output_size(upipe_fsrc, READ_SIZE));
    ubase_assert(upipe_set_uri(upipe_fsrc, src_file));

    /* TS demux */
    uprobe_init(&uprobe_demux_output_s, catch_ts_demux_output, uprobe_use(logger));
    uprobe_init(&uprobe_demux_program_s, catch_ts_demux_program, uprobe_use(logger));
    struct uprobe uprobe_ts_demux_s;
    uprobe_init(&uprobe_ts_demux_s, catch_ts_demux, uprobe_use(logger));

    struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    assert(upipe_mpgvf_mgr != NULL);
    struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
    assert(upipe_h264f_mgr != NULL);
    struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    assert(upipe_mpgaf_mgr != NULL);
    struct upipe_mgr *upipe_a52f_mgr = upipe_a52f_mgr_alloc();
    assert(upipe_a52f_mgr != NULL);

    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    assert(upipe_ts_demux_mgr != NULL);
    ubase_assert(upipe_ts_demux_mgr_set_mpgvf_mgr(upipe_ts_demux_mgr,
                                                  upipe_mpgvf_mgr));
    ubase_assert(upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr,
                                                  upipe_h264f_mgr));
    ubase_assert(upipe_ts_demux_mgr_set_mpgaf_mgr(upipe_ts_demux_mgr,
                                                  upipe_mpgaf_mgr));
    ubase_assert(upipe_ts_demux_mgr_set_a52f_mgr(upipe_ts_demux_mgr,
                                                 upipe_a52f_mgr));

    struct upipe *upipe_ts = upipe_void_alloc_output(upipe_fsrc,
            upipe_ts_demux_mgr,
            uprobe_pfx_alloc(&uprobe_ts_demux_s,
                             UPROBE_LOG_LEVEL, "ts demux"));
    assert(upipe_ts != NULL);
    upipe_mgr_release(upipe_ts_demux_mgr);
    upipe_mgr_release(upipe_mpgvf_mgr);
    upipe_mgr_release(upipe_h264f_mgr);
    upipe_mgr_release(upipe_mpgaf_mgr);
    upipe_mgr_release(upipe_a52f_mgr);
    upipe_mgr_release(upipe_fsrc_mgr);
    ubase_assert(upipe_ts_demux_set_conformance(upipe_ts,
                                                UPIPE_TS_CONFORMANCE_ISO));

    /* TS mux */
    struct upipe_mgr *upipe_ts_mux_mgr = upipe_ts_mux_mgr_alloc();
    assert(upipe_ts_mux_mgr != NULL);

    upipe_ts = upipe_void_chain_output(upipe_ts,
            upipe_ts_mux_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "ts mux"));
    assert(upipe_ts != NULL);
    upipe_mgr_release(upipe_ts_mux_mgr);
    ubase_assert(upipe_ts_mux_set_mode(upipe_ts, UPIPE_TS_MUX_MODE_CAPPED));
    ubase_assert(upipe_ts_mux_set_version(upipe_ts, 1));
    ubase_assert(upipe_ts_mux_set_cr_prog(upipe_ts, 0));

    /* file sink */
    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    assert(upipe_fsink_mgr != NULL);
    upipe_ts = upipe_void_chain_output(upipe_ts,
            upipe_fsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger),
                             UPROBE_LOG_LEVEL, "file sink"));
    assert(upipe_ts != NULL);
    upipe_mgr_release(upipe_fsink_mgr);
    ubase_assert(upipe_fsink_set_path(upipe_ts, sink_file, UPIPE_FSINK_OVERWRITE));

    upipe_release(upipe_ts);

    ev_loop(loop, 0);

    upipe_release(upipe_even);
    uprobe_release(logger);
    uprobe_clean(&uprobe_demux_output_s);
    uprobe_clean(&uprobe_demux_program_s);
    uprobe_clean(&uprobe_ts_demux_s);
    uprobe_clean(&uprobe_src_s);
    uprobe_clean(&uprobe_s);

    ev_default_destroy();
    return 0;
}

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
 */

/** @file
 * @short unit tests for TS demux and mux modules
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
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
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-ts/uprobe_ts_log.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_select_programs.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-ts/upipe_ts_mux.h>
#include <upipe-ts/upipe_ts_pat_decoder.h>
#include <upipe-ts/upipe_ts_pmt_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include <upipe-ts/upipe_ts_split.h>
#include <upipe-framers/upipe_mpgv_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe-framers/upipe_mpga_framer.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_file_sink.h>

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
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static struct upipe *upipe_ts_mux_program;
static struct uprobe *uprobe_ts_log;
static struct uprobe uprobe_demux_output_s;

/** generic probe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
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
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
        case UPROBE_TS_SPLIT_ADD_PID:
        case UPROBE_TS_SPLIT_DEL_PID:
        case UPROBE_TS_PATD_SYSTIME:
        case UPROBE_TS_PATD_TSID:
        case UPROBE_TS_PATD_ADD_PROGRAM:
        case UPROBE_TS_PATD_DEL_PROGRAM:
        case UPROBE_TS_PMTD_SYSTIME:
        case UPROBE_TS_PMTD_HEADER:
        case UPROBE_TS_PMTD_ADD_ES:
        case UPROBE_TS_PMTD_DEL_ES:
        case UPROBE_SOURCE_END:
        case UPROBE_NEW_FLOW_DEF:
            break;
    }
    return false;
}

/** probe to catch events from the TS demux outputs */
static bool catch_ts_demux_output(struct uprobe *uprobe, struct upipe *upipe,
                                  enum uprobe_event event, va_list args)
{
    if (event == UPROBE_SOURCE_END) {
        upipe_release(upipe);
        return true;
    }

    if (event != UPROBE_NEW_FLOW_DEF)
        return false;

    struct uref *flow_def = va_arg(args, struct uref *);
    uint64_t octetrate;
    assert(uref_block_flow_get_octetrate(flow_def, &octetrate));
    /* Free the previous output. */
    assert(upipe_set_output(upipe, NULL));

    struct upipe *mux_input = upipe_flow_alloc_sub(upipe_ts_mux_program,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "mux input"), flow_def);
    assert(mux_input != NULL);
    assert(upipe_set_output(upipe, mux_input));
    upipe_release(mux_input);
    return true;
}

/** probe to catch events from the TS demux */
static bool catch_ts_demux(struct uprobe *uprobe, struct upipe *upipe,
                           enum uprobe_event event, va_list args)
{
    if (event == UPROBE_SPLIT_DEL_FLOW)
        return true;
    if (event != UPROBE_SPLIT_ADD_FLOW)
        return false;

    uint64_t flow_id = va_arg(args, uint64_t);
    struct uref *flow_def = va_arg(args, struct uref *);
    assert(flow_def != NULL);
    const char *def = "(none)";
    if (!uref_flow_get_def(flow_def, &def) ||
        ubase_ncmp(def, "block.")) {
        upipe_warn_va(upipe, "flow def %s is not supported", def);
        return true;
    }

    upipe_dbg_va(upipe, "add flow %"PRIu64" (%s)", flow_id, def);

    struct upipe *output = upipe_flow_alloc_sub(upipe,
            uprobe_pfx_adhoc_alloc_va(&uprobe_demux_output_s,
                                      UPROBE_LOG_LEVEL, "demux output %"PRIu64,
                                      flow_id), flow_def);
    assert(output != NULL);
    return true;
}

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s <source file> <sink file>\n", argv0);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    const char *src_file, *sink_file;
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
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, -1,
                                                         -1, 0);
    assert(ubuf_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct uprobe uprobe_s;
    uprobe_init(&uprobe_s, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe_s, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    /* file source */
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    assert(upipe_fsrc_mgr != NULL);
    struct upipe *upipe_fsrc = upipe_void_alloc(upipe_fsrc_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "file source"));
    assert(upipe_fsrc != NULL);
    assert(upipe_set_upump_mgr(upipe_fsrc, upump_mgr));
    assert(upipe_set_uref_mgr(upipe_fsrc, uref_mgr));
    assert(upipe_set_ubuf_mgr(upipe_fsrc, ubuf_mgr));
    assert(upipe_source_set_read_size(upipe_fsrc, READ_SIZE));
    assert(upipe_set_uri(upipe_fsrc, src_file));

    /* TS demux */
    uprobe_ts_log = uprobe_ts_log_alloc(log, UPROBE_LOG_DEBUG);
    assert(uprobe_ts_log != NULL);
    uprobe_init(&uprobe_demux_output_s, catch_ts_demux_output, uprobe_ts_log);
    struct uprobe uprobe_ts_demux_s;
    uprobe_init(&uprobe_ts_demux_s, catch_ts_demux, uprobe_ts_log);
    struct uprobe *uprobe = uprobe_selflow_alloc(&uprobe_ts_demux_s,
                                  UPROBE_SELFLOW_PIC, "auto");
    assert(uprobe != NULL);
    uprobe = uprobe_selflow_alloc(uprobe, UPROBE_SELFLOW_SOUND, "auto");
    assert(uprobe != NULL);
    uprobe = uprobe_selflow_alloc(uprobe, UPROBE_SELFLOW_SUBPIC, "");
    assert(uprobe != NULL);
    uprobe = uprobe_selprog_alloc(uprobe, "auto");
    assert(uprobe != NULL);

    struct upipe_mgr *upipe_mpgvf_mgr = upipe_mpgvf_mgr_alloc();
    assert(upipe_mpgvf_mgr != NULL);
    struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
    assert(upipe_h264f_mgr != NULL);
    struct upipe_mgr *upipe_mpgaf_mgr = upipe_mpgaf_mgr_alloc();
    assert(upipe_mpgaf_mgr != NULL);

    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    assert(upipe_ts_demux_mgr != NULL);
    assert(upipe_ts_demux_mgr_set_mpgvf_mgr(upipe_ts_demux_mgr,
                                            upipe_mpgvf_mgr));
    assert(upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr,
                                            upipe_h264f_mgr));
    assert(upipe_ts_demux_mgr_set_mpgaf_mgr(upipe_ts_demux_mgr,
                                            upipe_mpgaf_mgr));

    struct uref *flow_def;
    assert(upipe_get_flow_def(upipe_fsrc, &flow_def));
    assert(flow_def != NULL);

    struct upipe *upipe_ts_demux = upipe_flow_alloc(upipe_ts_demux_mgr,
            uprobe_pfx_adhoc_alloc(uprobe, UPROBE_LOG_LEVEL, "ts demux"),
            flow_def);
    assert(upipe_ts_demux != NULL);
    assert(upipe_set_output(upipe_fsrc, upipe_ts_demux));

    /* TS mux */
    struct upipe_mgr *upipe_ts_mux_mgr = upipe_ts_mux_mgr_alloc();
    assert(upipe_ts_mux_mgr != NULL);
    flow_def = uref_alloc(uref_mgr);
    assert(flow_def != NULL);
    assert(uref_flow_set_def(flow_def, "void."));

    struct upipe *upipe_ts_mux = upipe_flow_alloc(upipe_ts_mux_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL, "ts mux"),
            flow_def);
    assert(upipe_ts_mux != NULL);
    uref_free(flow_def),
    assert(upipe_set_uref_mgr(upipe_ts_mux, uref_mgr));
    assert(upipe_set_ubuf_mgr(upipe_ts_mux, ubuf_mgr));
    //assert(upipe_ts_mux_set_padding_octetrate(upipe_ts_mux, 20000));
    //assert(upipe_ts_mux_set_mode(upipe_ts_mux, UPIPE_TS_MUX_MODE_CBR));

    flow_def = uref_program_flow_alloc_def(uref_mgr);
    assert(flow_def != NULL);
    upipe_ts_mux_program = upipe_flow_alloc_sub(upipe_ts_mux,
            uprobe_pfx_adhoc_alloc(uprobe_ts_log, UPROBE_LOG_LEVEL,
                                   "ts mux program"), flow_def);
    assert(upipe_ts_mux_program != NULL);
    uref_free(flow_def);

    /* file sink */
    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    assert(upipe_fsink_mgr != NULL);
    assert(upipe_get_flow_def(upipe_ts_mux, &flow_def));
    assert(flow_def != NULL);
    struct upipe *upipe_fsink = upipe_flow_alloc(upipe_fsink_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "file sink"),
            flow_def);
    assert(upipe_fsink != NULL);
    assert(upipe_set_upump_mgr(upipe_fsink, upump_mgr));
    assert(upipe_fsink_set_path(upipe_fsink, sink_file, UPIPE_FSINK_OVERWRITE));

    assert(upipe_set_output(upipe_ts_mux, upipe_fsink));

    ev_loop(loop, 0);

    upipe_release(upipe_ts_demux);
    upipe_release(upipe_ts_mux_program);
    upipe_release(upipe_ts_mux);
    upipe_release(upipe_fsrc);
    upipe_release(upipe_fsink);

    uprobe = uprobe_selprog_free(uprobe);
    uprobe = uprobe_selflow_free(uprobe);
    uprobe = uprobe_selflow_free(uprobe);
    uprobe = uprobe_selflow_free(uprobe);

    upipe_mgr_release(upipe_ts_demux_mgr);
    upipe_mgr_release(upipe_ts_mux_mgr);
    upipe_mgr_release(upipe_mpgvf_mgr);
    upipe_mgr_release(upipe_h264f_mgr);
    upipe_mgr_release(upipe_mpgaf_mgr);
    upipe_mgr_release(upipe_fsrc_mgr);
    upipe_mgr_release(upipe_fsink_mgr);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_ts_log_free(uprobe_ts_log);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();
    return 0;
}

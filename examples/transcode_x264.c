/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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

 /*

([qsrc][x264][fsink]) {border-style: dashed; flow: west}
[] -- stream --> [avfsrc] --> [avcdv] --> [qsink] --> {flow: south; end: east}
[qsrc] --> [x264] --> [fsink] -- file --> {flow: west}[]

     stream     +--------+     +-------+     +-------+
    -------->   | avfsrc | --> | avcdv | --> | qsink |   -+
                +--------+     +-------+     +-------+    |
                                                          |
              + - - - - - - - - - - - - - - - - - - - -+  |
              '                                        '  |
     file     ' +--------+     +-------+     +-------+ '  |
    <-------- ' | fsink  | <-- | x264  | <-- | qsrc  | ' <+
              ' +--------+     +-------+     +-------+ '
              '                                        '
              + - - - - - - - - - - - - - - - - - - - -+
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-av/upipe_avcodec_dec_vid.h>
#include <upipe-x264/upipe_x264.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <ev.h>



#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define QUEUE_LENGTH 50
#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0

enum upipe_fsink_mode mode = UPIPE_FSINK_OVERWRITE;
enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
struct uprobe *logger;
struct uprobe uprobe_outputs;

struct uref_mgr *uref_mgr;
struct ubuf_mgr *yuv_mgr;
struct ubuf_mgr *block_mgr;

struct upipe *qsink;
const char *sink_path = NULL;
const char *profile = NULL;
const char *preset = NULL;
const char *tuning = NULL;

/* clean outputs */
static bool catch_outputs(struct uprobe *uprobe, struct upipe *upipe,
                         enum uprobe_event event, va_list args)
{
    switch(event) {
        case UPROBE_NEED_INPUT:
        case UPROBE_READ_END:
            upipe_release(upipe);
            return true;
        default:
            return false;
    }
}

static bool catch_split(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            break;
        case UPROBE_NEED_INPUT:
        case UPROBE_DEAD:
        case UPROBE_READY:
        case UPROBE_SPLIT_DEL_FLOW:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
        case UPROBE_READ_END:
            return true;

        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *def = NULL;
            uref_flow_get_def(flow_def, &def);
            if (ubase_ncmp(def, "block.")) {
                upipe_warn_va(upipe,
                         "flow def %s (%d) is not supported", def, flow_id);
                break;
            }
            upipe_notice_va(upipe, "adding flow %s (%d)", def, flow_id);

            struct upipe *output = upipe_alloc_output(upipe,
                    uprobe_pfx_adhoc_alloc_va(&uprobe_outputs, loglevel,
                                                      "output %"PRIu64, flow_id));

            upipe_set_flow_def(output, flow_def);
            upipe_set_ubuf_mgr(output, block_mgr);

            struct upipe_mgr *upipe_avcdv_mgr = upipe_avcdv_mgr_alloc();
            struct upipe *avcdv = upipe_alloc(upipe_avcdv_mgr,
                    uprobe_pfx_adhoc_alloc_va(logger, loglevel, "avcdv"));
            upipe_set_uref_mgr(avcdv, uref_mgr);
            upipe_set_ubuf_mgr(avcdv, yuv_mgr);
            upipe_set_output(output, avcdv);
            upipe_release(avcdv);

            upipe_set_output(avcdv, qsink);
            upipe_release(qsink);
            return true;
        }
    }
    return false;
}

static void *encoding_thread(void *_qsrc)
{
    struct upipe *qsrc = _qsrc;

    printf("Starting encoding thread\n");

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    upipe_set_upump_mgr(qsrc, upump_mgr);

    struct upipe_mgr *upipe_x264_mgr = upipe_x264_mgr_alloc();
    struct upipe *x264 = upipe_alloc(upipe_x264_mgr,
            uprobe_pfx_adhoc_alloc_va(logger, loglevel, "x264"));
    upipe_set_ubuf_mgr(x264, block_mgr);
    upipe_set_uref_mgr(x264, uref_mgr);
    upipe_set_output(qsrc, x264);
    upipe_release(x264);
    if (preset || tuning) {
        upipe_x264_set_default_preset(x264, preset, tuning);
    }
    if (profile) {
        upipe_x264_set_profile(x264, profile);
    }

    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    struct upipe *sinkpipe = upipe_alloc(upipe_fsink_mgr,
            uprobe_pfx_adhoc_alloc(logger, loglevel, "fsink"));
    upipe_set_upump_mgr(sinkpipe, upump_mgr);
    upipe_fsink_set_path(sinkpipe, sink_path, mode);

    upipe_set_output(x264, sinkpipe);
    upipe_release(sinkpipe);

    ev_loop(loop, 0);

    return NULL;
}

void usage(const char *argv0)
{
    printf("Usage: %s [-d] [-p profile] [-s preset] [-g tuning] stream file.x264\n", argv0);
    exit(-1);
}

int main(int argc, char **argv)
{
    int opt;
    const char *url = NULL;

    /* parse options */
    while ((opt = getopt(argc, argv, "dp:s:g:")) != -1) {
        switch (opt) {
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
                break;
            case 'p':
                profile = optarg;
                break;
            case 's':
                preset = optarg;
                break;
            case 'g':
                tuning = optarg;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (optind >= argc -1) {
        usage(argv[0]);
    }

    url = argv[optind++];
    sink_path = argv[optind++];

    /* upipe env */
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                        UBUF_POOL_DEPTH, umem_mgr,
                                        -1, -1, -1, 0);
    yuv_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    /* planar YUV (I420) */
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "y8", 1, 1, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "u8", 2, 2, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "v8", 2, 2, 1);

    /* log probes */
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(NULL, stdout, loglevel);
    logger = uprobe_log_alloc(uprobe_stdio, loglevel);
    /* split probes */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch_split, logger);
    struct uprobe *uprobe_split = &uprobe;
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_PIC, "auto");
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_SOUND, "");
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_SUBPIC, "");
    /* output probe */
    uprobe_init(&uprobe_outputs, catch_outputs, logger);

    /* uclock */
    struct uclock *uclock = uclock_std_alloc(UCLOCK_FLAG_REALTIME);

    /* queue sink */
    struct upipe_mgr *upipe_qsink_mgr = upipe_qsink_mgr_alloc();
    qsink = upipe_alloc(upipe_qsink_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "qsink"));
    upipe_set_upump_mgr(qsink, upump_mgr);

    /* queue source */
    struct upipe_mgr *upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    struct upipe *qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
        uprobe_pfx_adhoc_alloc(&uprobe_outputs, loglevel, "qsrc"), QUEUE_LENGTH);
    upipe_qsink_set_qsrc(qsink, qsrc);

    /* launch encoding thread */
    pthread_t thread;
    memset(&thread, 0, sizeof(pthread_t));
    pthread_create(&thread, NULL, encoding_thread, qsrc);

    /* upipe-av */
    upipe_av_init(false);
    struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
    struct upipe *upipe_avfsrc = upipe_alloc(upipe_avfsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "avfsrc"));
    upipe_set_upump_mgr(upipe_avfsrc, upump_mgr);
    upipe_set_uref_mgr(upipe_avfsrc, uref_mgr);
    upipe_set_uclock(upipe_avfsrc, uclock);
    upipe_avfsrc_set_url(upipe_avfsrc, url); /* run this last */

    /* fire decode engine and main loop */
    printf("Starting main thread ev_loop\n");
    ev_loop(loop, 0);

    pthread_join(thread, NULL);

    upipe_release(upipe_avfsrc);
    upipe_av_clean();
    uclock_release(uclock);
    
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(yuv_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(logger);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();

    return 0;
}

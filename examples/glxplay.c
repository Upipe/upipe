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
graph {flow: east}
( [qsrc] [glx]) {border-style: dashed;}
[] -- stream --> [avfsrc]{rank: 0} -- encoded --> [avcdec] -- yuv --> [deint]
-- progressive --> {flow: south; end: east} [yuvrgb]
-- rgb --> {flow: west} [qsink] --> [qsrc] --> [glx]

  |
  | stream
  v
+---------+  encoded     +-------+  yuv   +--------+
| avfsrc  | --------->   | avcdec | -----> | deint  |   -+
+---------+              +-------+        +--------+    |
                                                        |
                                                        | progressive
                                                        |
                         +-------+  rgb   +--------+    |
                         | qsink | <----- | yuvrgb |   <+
                         +-------+        +--------+
                           |
                           |
                           v
                       +- - - - - - - - - - - - - - -+
                       '                             '
                       ' +-------+        +--------+ '
                       ' | qsrc  | -----> |  glx   | '
                       ' +-------+        +--------+ '
                       '                             '
                       +- - - - - - - - - - - - - - -+
 */

#include <stdlib.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdarg.h>

#undef NDEBUG

#include <libswscale/swscale.h>

#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_select_programs.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_queue_sink.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_udp_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-ts/upipe_ts_demux.h>
#include <upipe-framers/upipe_mp2v_framer.h>
#include <upipe-framers/upipe_h264_framer.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/uref_av_flow.h>
#include <upipe-av/upipe_avformat_source.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-swscale/upipe_sws.h>
#include <upipe-gl/upipe_glx_sink.h>
#include <upipe-gl/uprobe_gl_sink_cube.h>
#include <upipe-filters/upipe_filter_blend.h>

#include <ev.h>
#include <pthread.h>

#define ALIVE() { printf("ALIVE %s %d\n", __func__, __LINE__); fflush(stdout);}
#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define QUEUE_LENGTH 5
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define READ_SIZE 4096

#undef BENCH_TS

struct test_output {
    struct uchain uchain;
    struct upipe *upipe_avfsrc_output;
};

struct thread {
    pthread_t id;
};

const char *url = NULL;
struct upipe *qsrc;
struct upipe_mgr *glx_mgr;
struct upipe_mgr *avcdec_mgr;
struct upipe_mgr *upipe_filter_blend_mgr;
struct upipe_mgr *upipe_sws_mgr;
struct upipe_mgr *upipe_qsink_mgr;
struct ubuf_mgr *yuv_mgr;
struct ubuf_mgr *rgb_mgr = NULL;
struct ubuf_mgr *block_mgr;
struct upipe *output = NULL;
struct uprobe *logger;
struct uprobe uprobe_glx;
struct upump_mgr *upump_mgr_thread;
enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
bool paused = false;

/** catch callback (main thread) */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_SOURCE_END:
#ifndef BENCH_TS
            upipe_avfsrc_set_url(upipe, url);
#endif
            return true;

        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *def = NULL;
            uref_flow_get_def(flow_def, &def);
            if (ubase_ncmp(def, "block.")) {
                upipe_warn_va(upipe,
                             "flow def %s is not supported by unit test", def);
                return false;
            }

            upipe_err_va(upipe, "add flow %"PRIu64" (%s)", flow_id, def);
            assert(output == NULL);
            output = upipe_flow_alloc_sub(upipe,
                        uprobe_pfx_adhoc_alloc(uprobe, loglevel, "video"),
                        flow_def);
            assert(output != NULL);
            upipe_set_ubuf_mgr(output, block_mgr);

#ifndef BENCH_TS
            struct upipe *avcdec = upipe_flow_alloc(avcdec_mgr,
                    uprobe_pfx_adhoc_alloc_va(uprobe, loglevel, "avcdec"),
                    flow_def);
            assert(avcdec != NULL);
            upipe_set_ubuf_mgr(avcdec, yuv_mgr);
            upipe_set_output(output, avcdec);
            /* avcdec doesn't need upump if there is only one avcodec pipe
             * calling avcodec_open/_close at the same time */

            upipe_get_flow_def(avcdec, &flow_def);
            struct upipe *deint = upipe_flow_alloc(upipe_filter_blend_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe, loglevel, "deint"),
                    flow_def);
            assert(deint != NULL);
            upipe_set_ubuf_mgr(deint, yuv_mgr);
            upipe_set_output(avcdec, deint);
            upipe_release(avcdec);

            upipe_get_flow_def(deint, &flow_def);
            struct upipe *yuvrgb = upipe_flow_alloc(upipe_sws_mgr,
                    uprobe_pfx_adhoc_alloc_va(uprobe, loglevel, "rgb"),
                    flow_def);
            assert(yuvrgb != NULL);
            upipe_set_ubuf_mgr(yuvrgb, rgb_mgr);
            upipe_set_output(deint, yuvrgb);
            upipe_release(deint);

            upipe_get_flow_def(yuvrgb, &flow_def);
            struct upipe *qsink = upipe_flow_alloc(upipe_qsink_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe, loglevel, "qsink"),
                    flow_def);
            assert(qsink != NULL);
            upipe_set_output(yuvrgb, qsink);
            upipe_release(yuvrgb);

            upipe_qsink_set_qsrc(qsink, qsrc);
            upipe_release(qsink);
#else
            struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
            struct upipe *null = upipe_flow_alloc(upipe_null_mgr, uprobe,
                                                  flow_def);
            assert(null != NULL);
            upipe_mgr_release(upipe_null_mgr);
            upipe_set_output(output, null);
            upipe_release(null);
#endif
            return true;
        }
        default:
            return false;
    }
}

/** GUI keyhandler */
static void keyhandler(struct upipe *upipe, unsigned long key)
{
    switch (key) {
        case 27:
        case 'q': {
            upipe_notice_va(upipe, "exit key pressed (%d), exiting", key);
            exit(0);
        }
        case ' ': {
#if 0
            struct upump_mgr *upump_mgr = NULL;
            upipe_get_upump_mgr(upipe, &upump_mgr);
            if ( (paused = !paused) ) {
                upipe_notice(upipe, "Playback paused");
                upump_mgr_sink_block(upump_mgr);
            } else {
                upipe_notice(upipe, "Playback resumed");
                upump_mgr_sink_unblock(upump_mgr);
            }
#endif
            /* Should be done as a command to the glx pipe */
            break;
        }
        default:
            upipe_dbg_va(upipe, "key pressed (%d)", key);
            break;
    }
}

/** GUI thread catch callback */
static bool thread_catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_GLX_SINK_KEYPRESS: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_GLX_SINK_SIGNATURE);
            unsigned long key = va_arg(args, unsigned long);
            keyhandler(upipe, key);
            return true;
        }
        case UPROBE_GLX_SINK_KEYRELEASE:
            return true;
        default:
            break;
    }
    return false;
}

/** queue source catch callback */
static bool qsrc_catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            /* glx sink */
            struct upipe *glx_sink = upipe_flow_alloc(glx_mgr,
                    uprobe_gl_sink_cube_alloc(
                         uprobe_pfx_adhoc_alloc(&uprobe_glx, loglevel, "glx")),
                    flow_def);
            assert(glx_sink);
            upipe_set_upump_mgr(glx_sink, upump_mgr_thread);
            upipe_glx_sink_init(glx_sink, 0, 0, 800, 480);
            upipe_set_output(upipe, glx_sink);
            upipe_release(glx_sink);
            return true;
        }
        default:
            break;
    }
    return false;
}

/** gui thread idler */
static void thread_idler (struct upump *upump)
{
    unsigned int len;
    upipe_qsrc_get_length(qsrc, &len);
    // \r goes back to begining-of-line, and \003[2K cleans the whole line
    fprintf(stderr, "\r\033[2K\rQueue length: %u\r", len);
}

/** gui thread entrypoint */
static void *glx_thread (void *_thread)
{
    struct thread *thread = _thread;

    struct ev_loop *loop = ev_loop_new(0);
    upump_mgr_thread = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    upipe_set_upump_mgr(qsrc, upump_mgr_thread);

    uprobe_init(&uprobe_glx, thread_catch, logger);

    struct upump *idlepump = upump_alloc_timer(upump_mgr_thread, thread_idler, thread,
                                               0, 27000000/1000);
    upump_start(idlepump);

    // Fire
    ev_loop(loop, 0);

    return NULL;
}

int main(int argc, char** argv)
{
    bool upipe_ts = false;
    int opt;
    // parse options
    while ((opt = getopt(argc, argv, "dt")) != -1) {
        switch (opt) {
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
                break;
            case 't':
                upipe_ts = true;
                break;
            default:
                break;
        }
    }
    if (optind >= argc) {
        printf("Usage: %s [-d] [-t] filename\n", argv[0]);
        exit(-1);
    }

    url = argv[optind++];

    // upipe env
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
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                        UBUF_SHARED_POOL_DEPTH, umem_mgr,
                                        -1, -1, -1, 0);
    yuv_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    /* planar YUV (I420) */
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "y8", 1, 1, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "u8", 2, 2, 1);
    ubuf_pic_mem_mgr_add_plane(yuv_mgr, "v8", 2, 2, 1);
    /* rgb (glx_sink needs contigous rgb data for glTexImage2D) */
    rgb_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      0, UBUF_ALIGN_OFFSET);
    assert(rgb_mgr);
    ubuf_pic_mem_mgr_add_plane(rgb_mgr, "rgb24", 1, 1, 3);
    umem_mgr_release(umem_mgr);

    /* probes common to both threads */
    logger = uprobe_stdio_alloc(NULL, stdout, loglevel);
    logger = uprobe_log_alloc(logger, loglevel);

    /* probes specific to this thread */
    struct uprobe *uprobe = uprobe_uref_mgr_alloc(logger, uref_mgr);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    uref_mgr_release(uref_mgr);
    upump_mgr_release(upump_mgr);

    /* probes specific to the split pipe */
    struct uprobe uprobe_split_s;
    uprobe_init(&uprobe_split_s, catch, uprobe);
    struct uprobe *uprobe_split = &uprobe_split_s;
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_PIC, "auto");
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_SOUND, "");
    uprobe_split = uprobe_selflow_alloc(uprobe_split, UPROBE_SELFLOW_SUBPIC, "");
    if (upipe_ts)
        uprobe_split = uprobe_selprog_alloc(uprobe_split, "auto");

    upipe_filter_blend_mgr = upipe_filter_blend_mgr_alloc();
    upipe_sws_mgr = upipe_sws_mgr_alloc();
    upipe_qsink_mgr = upipe_qsink_mgr_alloc();

    // queue source
    struct uprobe uprobe_qsrc_s;
    uprobe_init(&uprobe_qsrc_s, qsrc_catch, logger);
    struct upipe_mgr *upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
                    uprobe_pfx_adhoc_alloc(&uprobe_qsrc_s, loglevel, "qsrc"),
                    QUEUE_LENGTH);

#ifndef BENCH_TS
    // Fire display engine
    printf("Starting glx thread\n");
    struct thread thread;
    memset(&thread, 0, sizeof(struct thread));
    pthread_create(&thread.id, NULL, glx_thread, &thread);
#endif

    // uclock
    struct uclock *uclock = uclock_std_alloc(0);

    glx_mgr = upipe_glx_sink_mgr_alloc();

    // upipe-av
    upipe_av_init(false);
    avcdec_mgr = upipe_avcdec_mgr_alloc();
    struct upipe *upipe_src;

    if (!upipe_ts) {
        /* use avformat source (and internal demuxer) */
        struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
        upipe_src = upipe_void_alloc(upipe_avfsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "avfsrc"));
        upipe_avfsrc_set_url(upipe_src, url);
        upipe_set_uclock(upipe_src, uclock);
        upipe_mgr_release(upipe_avfsrc_mgr);
    } else {
        /* try file source */
        struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
        upipe_src = upipe_void_alloc(upipe_fsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "fsrc"));
        upipe_mgr_release(upipe_fsrc_mgr);
        upipe_set_ubuf_mgr(upipe_src, block_mgr);
        if (!upipe_fsrc_set_path(upipe_src, url)) {
            /* try udp source */
            upipe_release(upipe_src);
            struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
            upipe_src = upipe_void_alloc(upipe_udpsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "udpsrc"));
            upipe_mgr_release(upipe_udpsrc_mgr);
            upipe_set_ubuf_mgr(upipe_src, block_mgr);
            if (!upipe_udpsrc_set_uri(upipe_src, url)) {
                /* try http source */
                upipe_release(upipe_src);
                struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
                upipe_src = upipe_void_alloc(upipe_http_src_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "http"));
                upipe_mgr_release(upipe_http_src_mgr);
                upipe_set_ubuf_mgr(upipe_src, block_mgr);
                if (!upipe_http_src_set_url(upipe_src, url)) {
                    upipe_release(upipe_src);
                    printf("invalid URL\n");
                    exit(EXIT_FAILURE);
                }
            }
        }

        struct uref *flow_def;
        upipe_get_flow_def(upipe_src, &flow_def);

        /* upipe-ts demuxer */
        struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
        struct upipe_mgr *upipe_mp2vf_mgr = upipe_mp2vf_mgr_alloc();
        upipe_ts_demux_mgr_set_mp2vf_mgr(upipe_ts_demux_mgr, upipe_mp2vf_mgr);
        upipe_mgr_release(upipe_mp2vf_mgr);
        struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
        upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr, upipe_h264f_mgr);
        upipe_mgr_release(upipe_h264f_mgr);
        struct upipe *ts_demux = upipe_flow_alloc(upipe_ts_demux_mgr,
                uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "ts demux"),
                flow_def);
        upipe_set_output(upipe_src, ts_demux);
        upipe_release(ts_demux);
    }

    // Fire decode engine
    printf("Starting main thread ev_loop\n");
    ev_loop(loop, 0);

    if (output != NULL)
        upipe_release(output);
    upipe_release(upipe_src);
    upipe_av_clean();
    uclock_release(uclock);

    if (upipe_ts) {
        uprobe_selprog_set(uprobe_split, "");
        uprobe_split = uprobe_selprog_free(uprobe_split);
    }
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe_split = uprobe_selflow_free(uprobe_split);
    uprobe = uprobe_upump_mgr_free(uprobe);
    uprobe = uprobe_uref_mgr_free(uprobe);
    uprobe = uprobe_log_free(logger);
    uprobe_stdio_free(uprobe);

    upipe_mgr_release(upipe_qsink_mgr);

    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(yuv_mgr);
    ubuf_mgr_release(rgb_mgr);

    ev_default_destroy();

//    pthread_join(thread.id, NULL);
    return 0; 
}


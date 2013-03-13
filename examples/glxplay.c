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
[] -- stream --> [avfsrc]{rank: 0} -- encoded --> [avcdv] -- yuv --> [deint]
-- progressive --> {flow: south; end: east} [yuvrgb]
-- rgb --> {flow: west} [qsink] --> [qsrc] --> [glx]

  |
  | stream
  v
+---------+  encoded     +-------+  yuv   +--------+
| avfsrc  | --------->   | avcdv | -----> | deint  |   -+
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
#include <upipe-av/upipe_avcodec_dec_vid.h>
#include <upipe-gl/upipe_glx_sink.h>
#include <upipe-gl/uprobe_gl_sink_cube.h>
#include <upipe-filters/upipe_filter_blend.h>

#include <ev.h>
#include <pthread.h>

#define ALIVE() { printf("ALIVE %s %d\n", __func__, __LINE__); fflush(stdout);}
#define UPROBE_LOG_LEVEL UPROBE_LOG_NOTICE
#define QUEUE_LENGTH 50
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define READ_SIZE 4096

#undef BENCH_TS

/*
 * upipe-yuv-rgb
 */

#define UPIPE_YUVRGB_SIGNATURE 0x424242
struct upipe_yuvrgb {
    struct SwsContext *swsctx;
    struct ubuf_mgr *ubuf_mgr;
    struct upipe *output;
    struct uref *output_flow;
    bool output_flow_sent;
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_yuvrgb, upipe);
UPIPE_HELPER_UBUF_MGR(upipe_yuvrgb, ubuf_mgr);
UPIPE_HELPER_OUTPUT(upipe_yuvrgb, output, output_flow, output_flow_sent)

static struct upipe *upipe_yuvrgb_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe)
{
    struct upipe_yuvrgb *upipe_yuvrgb = malloc(sizeof(struct upipe_yuvrgb));
    if (unlikely(upipe_yuvrgb == NULL)) {
        return NULL;
    }
    memset(upipe_yuvrgb, 0, sizeof(struct upipe_yuvrgb));
    struct upipe *upipe = upipe_yuvrgb_to_upipe(upipe_yuvrgb);
    upipe_init(upipe, mgr, uprobe);
    upipe_yuvrgb_init_ubuf_mgr(upipe);
    upipe_yuvrgb_init_output(upipe);
    upipe_throw_ready(upipe);
    return upipe;
}

static void upipe_yuvrgb_input(struct upipe *upipe, struct uref *uref,
                               struct upump *upump)
{
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        uref_flow_set_def(uref, "pic.rgb");
        upipe_yuvrgb_store_flow_def(upipe, uref);
        return;
    }
    if (unlikely(!uref->ubuf)) { // no ubuf in uref
        uref_free(uref);
        return;
    }

    struct upipe_yuvrgb *upipe_yuvrgb = upipe_yuvrgb_from_upipe(upipe);
    const uint8_t *sdata[4];
    uint8_t *ddata[4];
    int sline[4], dline[4], i;
    size_t stride = 0, width, height;
    const char *chroma = NULL;
    struct ubuf *ubuf_rgb = NULL;

    // Now process frames
    uref_pic_size(uref, &width, &height, NULL);
    upipe_dbg_va(upipe, "received pic (%dx%d)", width, height);

    // map input ubuf
    i = 0;
    while(uref_pic_plane_iterate(uref, &chroma) && chroma && i < 3) {
        uref_pic_plane_read(uref, chroma, 0, 0, -1, -1, &sdata[i]);
        uref_pic_plane_size(uref, chroma, &stride, NULL, NULL, NULL);
        sline[i] = (int) stride;
        i++;
    }

    // Alloc rgb buffer
    assert(upipe_yuvrgb->ubuf_mgr);
    ubuf_rgb = ubuf_pic_alloc(upipe_yuvrgb->ubuf_mgr, width, height);
    assert(ubuf_rgb);

    // map output ubuf
    ddata[0] = ddata[1] = ddata[2] = ddata[3] = NULL;
    dline[0] = dline[1] = dline[2] = dline[3] = 0;
    ubuf_pic_plane_write(ubuf_rgb, "rgb24", 0, 0, -1, -1, &ddata[0]);
    ubuf_pic_plane_size(ubuf_rgb, "rgb24", &stride, NULL, NULL, NULL);
    dline[0] = (int) stride;

    // init swscale context
    upipe_yuvrgb->swsctx = sws_getCachedContext(upipe_yuvrgb->swsctx, width, height,
            PIX_FMT_YUV420P, width, height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
    // fire conversion
    sws_scale(upipe_yuvrgb->swsctx, sdata, sline, 0, height, ddata, dline);

    // unmap output
    ubuf_pic_plane_unmap(ubuf_rgb, "rgb24", 0, 0, -1, -1);

    // unmap input
    chroma = NULL;
    i = 0;
    while(uref_pic_plane_iterate(uref, &chroma) && chroma && i < 3) {
        uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
        i++;
    }

    ubuf_free(uref_detach_ubuf(uref));
    uref_attach_ubuf(uref, ubuf_rgb);

    upipe_yuvrgb_output(upipe, uref, upump);
}

static bool upipe_yuvrgb_control(struct upipe *upipe,
                               enum upipe_command command, va_list args)
{
    switch (command) {
        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_yuvrgb_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_yuvrgb_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_yuvrgb_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_yuvrgb_set_output(upipe, output);
        }
        default:
            return false;
    }
}

static void upipe_yuvrgb_free(struct upipe *upipe)
{
    struct upipe_yuvrgb *upipe_yuvrgb = upipe_yuvrgb_from_upipe(upipe);
    upipe_throw_dead(upipe);

    if (upipe_yuvrgb->swsctx) {
        sws_freeContext(upipe_yuvrgb->swsctx);
    }

    upipe_yuvrgb_clean_ubuf_mgr(upipe);
    upipe_yuvrgb_clean_output(upipe);
    upipe_clean(upipe);
    free(upipe_yuvrgb);
}

static struct upipe_mgr upipe_yuvrgb_mgr = {
    .signature = UPIPE_YUVRGB_SIGNATURE,
    .upipe_alloc = upipe_yuvrgb_alloc,
    .upipe_input = upipe_yuvrgb_input,
    .upipe_control = upipe_yuvrgb_control,
    .upipe_free = upipe_yuvrgb_free,

    .upipe_mgr_free = NULL
};

/*
 * example specific code
 */

struct test_output {
    struct uchain uchain;
    struct upipe *upipe_avfsrc_output;
};

struct thread {
    pthread_t id;
    struct upipe *qsrc;
};

const char *url = NULL;
struct upipe *upipe_qsink;
struct upipe_mgr *avcdv_mgr;
struct upipe_mgr *upipe_filter_blend_mgr;
struct ubuf_mgr *yuv_mgr;
struct ubuf_mgr *rgb_mgr = NULL;
struct ubuf_mgr *block_mgr;
struct upipe *output = NULL;
struct uprobe *logger;
enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
bool paused = false;

/** catch callback (main thread) */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_READ_END:
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
            output = upipe_alloc_output(upipe,
                    uprobe_pfx_adhoc_alloc(uprobe, loglevel, "video"));
            assert(output != NULL);

#ifndef BENCH_TS
            struct upipe *avcdv = upipe_alloc(avcdv_mgr,
                    uprobe_pfx_adhoc_alloc_va(uprobe, loglevel, "avcdv"));
            assert(avcdv != NULL);
            upipe_set_ubuf_mgr(avcdv, yuv_mgr);
            /* avcdv doesn't need upump if there is only one avcodec pipe
             * calling avcodec_open/_close at the same time */

            struct upipe *deint = upipe_alloc(upipe_filter_blend_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe, loglevel, "deint"));
            assert(deint != NULL);
            upipe_set_ubuf_mgr(deint, yuv_mgr);

            struct upipe *yuvrgb = upipe_alloc(&upipe_yuvrgb_mgr,
                    uprobe_pfx_adhoc_alloc_va(uprobe, loglevel, "rgb"));
            assert(yuvrgb != NULL);
            upipe_set_ubuf_mgr(yuvrgb, rgb_mgr);

            upipe_set_output(yuvrgb, upipe_qsink);
            upipe_set_output(deint, yuvrgb);
            upipe_release(yuvrgb);
            upipe_set_output(avcdv, deint);
            upipe_release(deint);
            upipe_set_flow_def(output, flow_def);
            upipe_set_ubuf_mgr(output, block_mgr);
            upipe_set_output(output, avcdv);
            upipe_release(avcdv);
#else
            struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
            struct upipe *null = upipe_alloc(upipe_null_mgr, uprobe);
            assert(null != NULL);
            upipe_mgr_release(upipe_null_mgr);
            upipe_set_flow_def(output, flow_def);
            upipe_set_ubuf_mgr(output, block_mgr);
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
            struct upump_mgr *upump_mgr = NULL;
            upipe_get_upump_mgr(upipe, &upump_mgr);
            if ( (paused = !paused) ) {
                upipe_notice(upipe, "Playback paused");
                upump_mgr_sink_block(upump_mgr);
            } else {
                upipe_notice(upipe, "Playback resumed");
                upump_mgr_sink_unblock(upump_mgr);
            }
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

/** gui thread idler */
static void thread_idler (struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread*);
    unsigned int len;
    upipe_qsrc_get_length(thread->qsrc, &len);
    // \r goes back to begining-of-line, and \003[2K cleans the whole line
    fprintf(stderr, "\r\033[2K\rQueue length: %u\r", len);
}

/** gui thread entrypoint */
static void *glx_thread (void *_thread)
{
    struct thread *thread = _thread;

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    upipe_set_upump_mgr(thread->qsrc, upump_mgr);

    struct uprobe uprobe;
    uprobe_init(&uprobe, thread_catch, logger);

    /* glx sink */
    struct upipe_mgr *glx_mgr = upipe_glx_sink_mgr_alloc();
    struct upipe *glx_sink = upipe_alloc(glx_mgr, uprobe_gl_sink_cube_alloc(
                             uprobe_pfx_adhoc_alloc(&uprobe, loglevel, "glx")));
    assert(glx_sink);
    upipe_set_upump_mgr(glx_sink, upump_mgr);
    upipe_glx_sink_init(glx_sink, 0, 0, 800, 480);
    upipe_set_output(thread->qsrc, glx_sink);

    struct upump *idlepump = upump_alloc_timer(upump_mgr, thread_idler, thread,
                                               false, 0, 27000000/1000);
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
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
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

    // queue sink
    struct upipe_mgr *upipe_qsink_mgr = upipe_qsink_mgr_alloc();
    upipe_qsink = upipe_alloc(upipe_qsink_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe, loglevel, "qsink"));
    //upipe_set_upump_mgr(upipe_qsink, upump_mgr);

    // queue source
    struct upipe_mgr *upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    struct upipe *upipe_qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "qsrc"), QUEUE_LENGTH);

    upipe_qsink_set_qsrc(upipe_qsink, upipe_qsrc);
    upipe_release(upipe_qsrc);

#ifndef BENCH_TS
    // Fire display engine
    printf("Starting glx thread\n");
    struct thread thread;
    memset(&thread, 0, sizeof(struct thread));
    thread.qsrc = upipe_qsrc;
    pthread_create(&thread.id, NULL, glx_thread, &thread);
#endif

    // uclock
    struct uclock *uclock = uclock_std_alloc(0);

    // upipe-av
    upipe_av_init(false);
    avcdv_mgr = upipe_avcdv_mgr_alloc();
    struct upipe *upipe_src;

    if (!upipe_ts) {
        struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
        upipe_src = upipe_alloc(upipe_avfsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "avfsrc"));
        upipe_avfsrc_set_url(upipe_src, url);
        upipe_set_uclock(upipe_src, uclock);
        upipe_mgr_release(upipe_avfsrc_mgr);
    } else {
        struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
        upipe_src = upipe_alloc(upipe_fsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "fsrc"));
        upipe_mgr_release(upipe_fsrc_mgr);
        upipe_set_ubuf_mgr(upipe_src, block_mgr);
        if (!upipe_fsrc_set_path(upipe_src, url)) {
            upipe_release(upipe_src);
            struct upipe_mgr *upipe_udpsrc_mgr = upipe_udpsrc_mgr_alloc();
            upipe_src = upipe_alloc(upipe_udpsrc_mgr,
                    uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "udpsrc"));
            upipe_mgr_release(upipe_udpsrc_mgr);
            upipe_set_ubuf_mgr(upipe_src, block_mgr);
            if (!upipe_udpsrc_set_uri(upipe_src, url)) {
                upipe_release(upipe_src);
                printf("invalid URL\n");
                exit(EXIT_FAILURE);
            }
        }

        struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
        struct upipe_mgr *upipe_mp2vf_mgr = upipe_mp2vf_mgr_alloc();
        upipe_ts_demux_mgr_set_mp2vf_mgr(upipe_ts_demux_mgr, upipe_mp2vf_mgr);
        upipe_mgr_release(upipe_mp2vf_mgr);
        struct upipe_mgr *upipe_h264f_mgr = upipe_h264f_mgr_alloc();
        upipe_ts_demux_mgr_set_h264f_mgr(upipe_ts_demux_mgr, upipe_h264f_mgr);
        upipe_mgr_release(upipe_h264f_mgr);
        struct upipe *ts_demux = upipe_alloc(upipe_ts_demux_mgr,
                uprobe_pfx_adhoc_alloc(uprobe_split, loglevel, "ts demux"));
        upipe_set_output(upipe_src, ts_demux);
        upipe_release(ts_demux);
    }

    // Fire decode engine
    printf("Starting main thread ev_loop\n");
    ev_loop(loop, 0);

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

    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(yuv_mgr);
    ubuf_mgr_release(rgb_mgr);

    ev_default_destroy();

//    pthread_join(thread.id, NULL);
    return 0; 
}


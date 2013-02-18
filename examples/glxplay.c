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

#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
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
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_queue_sink.h>
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
#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define READ_SIZE 4096

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
    urefcount refcount;
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
    urefcount_init(&upipe_yuvrgb->refcount);
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

static void upipe_yuvrgb_use(struct upipe *upipe)
{
    struct upipe_yuvrgb *upipe_yuvrgb = upipe_yuvrgb_from_upipe(upipe);
    urefcount_use(&upipe_yuvrgb->refcount);
}

static void upipe_yuvrgb_release(struct upipe *upipe)
{
    struct upipe_yuvrgb *upipe_yuvrgb = upipe_yuvrgb_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_yuvrgb->refcount))) {
        upipe_throw_dead(upipe);

        if (upipe_yuvrgb->swsctx) {
            sws_freeContext(upipe_yuvrgb->swsctx);
        }

        upipe_yuvrgb_clean_ubuf_mgr(upipe);
        upipe_yuvrgb_clean_output(upipe);
        upipe_clean(upipe);
        urefcount_clean(&upipe_yuvrgb->refcount);
        free(upipe_yuvrgb);
    }
}

static struct upipe_mgr upipe_yuvrgb_mgr = {
    .signature = UPIPE_YUVRGB_SIGNATURE,
    .upipe_alloc = upipe_yuvrgb_alloc,
    .upipe_input = upipe_yuvrgb_input,
    .upipe_control = upipe_yuvrgb_control,
    .upipe_release = upipe_yuvrgb_release,
    .upipe_use = upipe_yuvrgb_use,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
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
struct upipe_mgr *upipe_null_mgr;
struct upipe_mgr *upipe_filter_blend_mgr;
struct uref_mgr *uref_mgr;
struct ubuf_mgr *yuv_mgr;
struct ubuf_mgr *rgb_mgr = NULL;
struct ubuf_mgr *block_mgr;
struct uprobe *logger;
struct upipe *upipe_avfsrc;
struct ulist upipe_avfsrc_outputs;
enum uprobe_log_level loglevel = UPROBE_LOG_LEVEL;
bool paused = false;

const char * video_codecs[] = {
    "mpeg1video",
    "mpeg2video",
    "h264",
    "mpeg4",
    "msmpeg4v1",
    "msmpeg4v2",
    "msmpeg4v3",
    NULL
};

static bool is_video(const char *name)
{
    int i;
    for (i=0; video_codecs[i]; i++) {
        if (!strcmp(video_codecs[i], name)) {
            return true;
        }
    }
    return false;
}

/** main thread catch callback */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    struct upipe *upipe_sink;
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_DEAD:
        case UPROBE_READY:
        case UPROBE_SPLIT_DEL_FLOW:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_CLOCK_REF:
        case UPROBE_CLOCK_TS:
            break;
        case UPROBE_READ_END: {
            upipe_avfsrc_set_url(upipe, url);
            break;
        }

        case UPROBE_SPLIT_ADD_FLOW: {
            uint64_t flow_id = va_arg(args, uint64_t);
            struct uref *flow_def = va_arg(args, struct uref *);
            const char *def = NULL;
            uref_flow_get_def(flow_def, &def);
            if (ubase_ncmp(def, "block.")) {
                upipe_warn_va(upipe,
                             "flow def %s is not supported by unit test", def);
                break;
            }

            uint64_t id = 0;
            uref_av_flow_get_id(flow_def, &id);

            struct test_output *output = malloc(sizeof(struct test_output));
            assert(output != NULL);
            uchain_init(&output->uchain);
            ulist_add(&upipe_avfsrc_outputs, &output->uchain);
            output->upipe_avfsrc_output = upipe_alloc_output(upipe_avfsrc,
                    uprobe_pfx_adhoc_alloc_va(logger, loglevel,
                                                      "output %"PRIu64, id));
            assert(output->upipe_avfsrc_output != NULL);

            if (is_video(def+strlen("block."))) {
                printf("probe: %s is a video codec\n", def);
                upipe_sink = upipe_alloc(avcdv_mgr,
                    uprobe_pfx_adhoc_alloc_va(logger, loglevel,
                                                          "avcdv %"PRIu64, id));
                upipe_set_uref_mgr(upipe_sink, uref_mgr);
                upipe_set_ubuf_mgr(upipe_sink, yuv_mgr);
                /* avcdv doesn't need upump if there is only one avcodec pipe
                 * calling avcodec_open/_close at the same time */
                struct upipe *deint = upipe_alloc(upipe_filter_blend_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "deint"));
                upipe_set_ubuf_mgr(deint, yuv_mgr);
                struct upipe *yuvrgb = upipe_alloc(&upipe_yuvrgb_mgr,
                    uprobe_pfx_adhoc_alloc_va(logger, loglevel,
                                                          "rgb %"PRIu64, id));
                upipe_set_ubuf_mgr(yuvrgb, rgb_mgr);
                upipe_set_output(upipe_sink, deint);
                upipe_release(deint);
                upipe_set_output(deint, yuvrgb);
                upipe_release(yuvrgb);
                upipe_set_output(yuvrgb, upipe_qsink);
                upipe_release(upipe_qsink);
            } else {
                upipe_sink = upipe_alloc(upipe_null_mgr,
                    uprobe_pfx_adhoc_alloc_va(logger, loglevel,
                                                          "sink %"PRIu64, id));
            }
            upipe_set_flow_def(output->upipe_avfsrc_output, flow_def);
            upipe_set_ubuf_mgr(output->upipe_avfsrc_output, block_mgr);
            upipe_set_output(output->upipe_avfsrc_output, upipe_sink);
            upipe_release(upipe_sink);
            break;
        }
    }
    return true;
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

    struct uprobe *logger = uprobe_log_alloc(NULL, loglevel);
    struct uprobe uprobe;
    uprobe_init(&uprobe, thread_catch, logger);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout, loglevel);

    /* glx sink */
    struct upipe_mgr *glx_mgr = upipe_glx_sink_mgr_alloc();
    struct upipe *glx_sink = upipe_alloc(glx_mgr, uprobe_gl_sink_cube_alloc(
                                uprobe_pfx_adhoc_alloc(uprobe_stdio, loglevel, "glx")));
    assert(glx_sink);
    upipe_set_upump_mgr(glx_sink, upump_mgr);
    upipe_glx_sink_init(glx_sink, 0, 0, 800, 480);
    upipe_set_output(thread->qsrc, glx_sink);

    struct upump *idlepump = upump_alloc_timer(upump_mgr, thread_idler, thread,
                                               false, 0, 27000000/1000);
    upump_start(idlepump);

    // Fire
    ev_loop(loop, 0);

    uprobe_log_free(logger);
    uprobe_stdio_free(uprobe_stdio);
    return NULL;
}

int main(int argc, char** argv)
{
    int opt;
    // parse options
    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                loglevel = UPROBE_LOG_DEBUG;
                break;
            default:
                break;
        }
    }
    if (optind >= argc) {
        printf("Usage: %s [-d] filename\n", argv[0]);
        exit(-1);
    }

    url = argv[optind++];

    // upipe env
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);
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
    /* rgb (glx_sink needs contigous rgb data for glTexImage2D) */
    rgb_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      0, UBUF_ALIGN_OFFSET);
    assert(rgb_mgr);
    ubuf_pic_mem_mgr_add_plane(rgb_mgr, "rgb24", 1, 1, 3);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     loglevel);
    logger = uprobe_log_alloc(uprobe_stdio, loglevel);

    upipe_null_mgr = upipe_null_mgr_alloc();
    upipe_filter_blend_mgr = upipe_filter_blend_mgr_alloc();

    // queue sink
    struct upipe_mgr *upipe_qsink_mgr = upipe_qsink_mgr_alloc();
    upipe_qsink = upipe_alloc(upipe_qsink_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "qsink"));
    upipe_set_upump_mgr(upipe_qsink, upump_mgr);

    // queue source
    struct upipe_mgr *upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    struct upipe *upipe_qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "qsrc"), QUEUE_LENGTH);

    upipe_qsink_set_qsrc(upipe_qsink, upipe_qsrc);
    upipe_release(upipe_qsrc);

    // Fire display engine
    printf("Starting glx thread\n");
    struct thread thread;
    memset(&thread, 0, sizeof(struct thread));
    thread.qsrc = upipe_qsrc;
    pthread_create(&thread.id, NULL, glx_thread, &thread);

    // uclock
    struct uclock *uclock = uclock_std_alloc(0);

    // upipe-av
    upipe_av_init(false);
    ulist_init(&upipe_avfsrc_outputs);
    avcdv_mgr = upipe_avcdv_mgr_alloc();
    struct upipe_mgr *upipe_avfsrc_mgr = upipe_avfsrc_mgr_alloc();
    upipe_avfsrc = upipe_alloc(upipe_avfsrc_mgr,
                    uprobe_pfx_adhoc_alloc(logger, loglevel, "avfsrc"));
    upipe_set_upump_mgr(upipe_avfsrc, upump_mgr);
    upipe_set_uref_mgr(upipe_avfsrc, uref_mgr);
    upipe_avfsrc_set_url(upipe_avfsrc, url);
    upipe_set_uclock(upipe_avfsrc, uclock);

    // Fire decode engine
    printf("Starting main thread ev_loop\n");
    ev_loop(loop, 0);

    // Now clean everything
    struct uchain *uchain;
    ulist_delete_foreach(&upipe_avfsrc_outputs, uchain) {
        struct test_output *output = container_of(uchain, struct test_output,
                                                  uchain);
        ulist_delete(&upipe_avfsrc_outputs, uchain);
        upipe_release(output->upipe_avfsrc_output);
        free(output);
    }

    upipe_release(upipe_avfsrc);
    upipe_av_clean();
    uclock_release(uclock);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(yuv_mgr);
    ubuf_mgr_release(rgb_mgr);

    ev_default_destroy();

//    pthread_join(thread.id, NULL);
    return 0; 
}


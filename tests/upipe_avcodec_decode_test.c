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
 */

#undef NDEBUG

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <ev.h>
#include <pthread.h>

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
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/upipe_helper_upipe.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "../lib/upipe-av/upipe_av_internal.h"

#define ALIVE(a) { printf("# ALIVE[%d]: %s %s - %d\n", (a), __FILE__, __func__, __LINE__); fflush(stdout); }

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1
#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define THREAD_NUM          16
#define ITER_LIMIT          1000
#define FRAMES_LIMIT        200
#define THREAD_FRAMES_LIMIT 200

/** @internal */
enum plane_action {
    UNMAP,
    READ,
    WRITE
};

/** @internal */
struct thread {
    pthread_t id;
    unsigned int num;
    unsigned int iteration;
    struct upipe *avcdec;
    struct upipe *audiodec;
    const char *codec_def;
    const char *audio_def;
    struct upump *fetchav_pump;

    int count;
    int limit;
    int videoStream;
    bool extrasent;
    struct upipe *pipe;
    AVFormatContext *avfctx;
};

/** Global vars */
int videoStream;
int audioStream;
struct uref_mgr *uref_mgr;
struct ubuf_mgr *block_mgr;
const char *pgm_prefix = NULL;

/** Save picture to pgm file */
static void pgm_save(const uint8_t *buf, int wrap, int xsize, int ysize, int num, const char *prefix) // FIXME debug
{
    char filename[256];
    FILE *f;
    int i;
    snprintf(filename, sizeof(filename), "%s-%04d.pgm", prefix, num);
    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n",xsize,ysize,255);
    for(i=0;i<ysize;i++)
        fwrite(buf + i * wrap,1,xsize,f);
    fclose(f);
}

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe, enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_SOURCE_END:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_NEED_UBUF_MGR:
        default:
            assert(event & UPROBE_HANDLED_FLAG);
            break;
    }
    return true;
}

/** phony pipe to test upipe_avcdec */
struct avcdec_test {
    struct upipe upipe;
};

/** helper phony pipe to test upipe_avcdec */
UPIPE_HELPER_UPIPE(avcdec_test, upipe, 0);

/** helper phony pipe to test upipe_avcdec */
static struct upipe *avcdec_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct avcdec_test *avcdec_test = malloc(sizeof(struct avcdec_test));
    if (unlikely(!avcdec_test)) return NULL;
    upipe_init(&avcdec_test->upipe, mgr, uprobe);
    upipe_throw_ready(&avcdec_test->upipe);
    return &avcdec_test->upipe;
}

/** helper phony pipe to test upipe_avcdec */
static void avcdec_test_input(struct upipe *upipe, struct uref *uref, struct upump *upump)
{
    const uint8_t *buf = NULL;
    size_t stride = 0, hsize = 0, vsize = 0;
    static int counter = 0;

    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);
    if (uref->ubuf) {
        uref_pic_plane_read(uref, "y8", 0, 0, -1, -1, &buf);
        uref_pic_plane_size(uref, "y8", &stride, NULL, NULL, NULL);
        uref_pic_size(uref, &hsize, &vsize, NULL);
        pgm_save(buf, stride, hsize, vsize, counter, pgm_prefix);
        uref_pic_plane_unmap(uref, "y8", 0, 0, -1, -1);
        counter++;
    }
    uref_free(uref);
    // FIXME peek into buffer
}

/** helper phony pipe to test upipe_avcdec */
static void avcdec_test_free(struct upipe *upipe)
{
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);
    struct avcdec_test *avcdec_test = avcdec_test_from_upipe(upipe);
    upipe_clean(upipe);
    free(avcdec_test);
}

/** helper phony pipe to test upipe_avcdec */
static struct upipe_mgr avcdec_test_mgr = {
    .signature = 0,
    .upipe_alloc = avcdec_test_alloc,
    .upipe_input = avcdec_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

/** nullpipe (/dev/null) */
static struct upipe *nullpipe_alloc(struct upipe_mgr *mgr,
                                    struct uprobe *uprobe,
                                    uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    if (unlikely(!upipe))
        return NULL;
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** nullpipe (/dev/null) */
static void nullpipe_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** nullpipe (/dev/null) */
static void nullpipe_input(struct upipe *upipe, struct uref *uref, struct upump *upump)
{
    upipe_dbg(upipe, "sending uref to devnull");
    uref_free(uref);
}

/** nullpipe (/dev/null) */
static struct upipe_mgr nullpipe_mgr = {
    .upipe_alloc = nullpipe_alloc,
    .upipe_input = nullpipe_input,
    .upipe_control = NULL,
    .upipe_free = nullpipe_free,
    .upipe_mgr_free = NULL
};

/** Fetch video packets using avformat and send them to avcdec pipe.
 * Also send extradata if present. */
static void fetch_av_packets(struct upump *pump)
{
    uint8_t *buf = NULL;
    AVPacket avpkt;
    struct uref *uref;
    int size;
    struct thread *thread = upump_get_opaque(pump, struct thread*);
    struct upipe *avcdec = thread->avcdec;
    assert(avcdec);

    if (thread->count < thread->limit && av_read_frame(thread->avfctx, &avpkt) >= 0) {
        if(avpkt.stream_index == videoStream) {
            size = avpkt.size;
            printf("#[%d]# Reading video frame %d - size : %d\n", thread->num, thread->count, size, avpkt.data);

            // Allocate uref/ubuf_block and copy data
            uref = uref_block_alloc(uref_mgr, block_mgr, size);
            uref_block_write(uref, 0, &size, &buf);
            memcpy(buf, avpkt.data, size);
            uref_block_unmap(uref, 0);

            // Send uref to avcdec pipe and free avpkt
            upipe_input(avcdec, uref, pump);
            thread->count++;
        } else if (thread->audiodec && avpkt.stream_index == audioStream) {
            size = avpkt.size;
            printf("#[%d]# Reading audio %d - size : %d\n", thread->num, thread->count, size, avpkt.data);
            // Allocate uref/ubuf_block and copy data
            uref = uref_block_alloc(uref_mgr, block_mgr, size);
            uref_block_write(uref, 0, &size, &buf);
            memcpy(buf, avpkt.data, size);
            uref_block_unmap(uref, 0);

            // Send uref to audiodec pipe and free avpkt
            upipe_input(thread->audiodec, uref, pump);
        }
        av_free_packet(&avpkt);
    } else {
        // Send empty uref to output last frame (move to decoder ?)
        upipe_release(thread->avcdec);
        if (thread->audiodec) {
            upipe_release(thread->audiodec);
        }
        upump_stop(pump);
    }
}

static void setcodec_idler(struct upump *pump)
{
    struct thread *thread = upump_get_opaque(pump, struct thread *);
    struct upipe *avcdec = thread->avcdec;

    if (thread->iteration >= ITER_LIMIT) {
        upump_stop(pump);
        upump_start(thread->fetchav_pump);
//        upipe_release(avcdec);
        return;
    }
    thread->iteration++;
}

/** Thread function from which ev_loops are launched.
 * This allows us to test avcdec/udeal */
static void *test_thread(void *_thread)
{
    struct thread *thread = ((struct thread *)_thread);
    struct upipe *avcdec = thread->avcdec;

    printf("Thread %d launched.\n", thread->num);
    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert (upump_mgr != NULL);

    assert(upipe_set_upump_mgr(avcdec, upump_mgr));

    struct upump *setcodec_pump = upump_alloc_idler(upump_mgr, setcodec_idler, thread);
    assert(setcodec_pump);
    upump_start(setcodec_pump);

    thread->limit = THREAD_FRAMES_LIMIT;
    thread->fetchav_pump = upump_alloc_idler(upump_mgr, fetch_av_packets, thread);
    assert(thread->fetchav_pump);

    // Fire !
    ev_loop(loop, 0);

    printf("Thread %d ended.\n", thread->num);
    upump_free(setcodec_pump);
    upump_free(thread->fetchav_pump);
    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);
}

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-n threads] <source file> [pgmprefix]\n", argv0);
    exit(EXIT_FAILURE);
}

int main (int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    int opt;
    int thread_num = THREAD_NUM;
    while ((opt = getopt(argc, argv, "n:")) != -1) {
        switch(opt) {
            case 'n':
                thread_num = strtod(optarg, NULL);
                break;
            default:
                usage(argv[0]);
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
    }
    const char *srcpath = argv[optind++];
    if (argc > optind) {
        pgm_prefix = argv[optind++];
    }

    int i, j;
    AVDictionary *options = NULL;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* block */
    block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr,
            UBUF_PREPEND, UBUF_APPEND,
            UBUF_ALIGN,
            UBUF_ALIGN_OFFSET);
    assert(block_mgr);

    /* planar YUV (I420) */
    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    assert(pic_mgr != NULL);
    assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8", 1, 1, 1));
    assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "u8", 2, 2, 1));
    assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "v8", 2, 2, 1));


    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    /* ev / pumps */
    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct thread mainthread;
    struct upump *write_pump = upump_alloc_idler(upump_mgr, fetch_av_packets, &mainthread);
    assert(write_pump);
    upump_start(write_pump);

    // main thread description
    memset(&mainthread, 0, sizeof(struct thread));
    mainthread.limit = FRAMES_LIMIT;
    mainthread.num = -1;

    // Open file with avformat
    printf("Trying to open %s ...\n", srcpath);
    assert(upipe_av_init(false, log));
    avformat_open_input(&mainthread.avfctx, srcpath, NULL, NULL);
    assert(mainthread.avfctx);
    assert(avformat_find_stream_info(mainthread.avfctx, NULL) >= 0);
    av_dump_format(mainthread.avfctx, 0, srcpath, 0);

    // Find first video stream
    videoStream = -1;
    audioStream = -1;
    for (i=0; i < mainthread.avfctx->nb_streams; i++) {
        switch (mainthread.avfctx->streams[i]->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                videoStream = i;
                break;
            case AVMEDIA_TYPE_AUDIO:
                audioStream = i;
                break;
            default:
                break;
        }
        if (videoStream >= 0 && audioStream >= 0) {
            break;
        }
    }

    assert(videoStream != -1);

    // set codec def and test _context()/_release()
    mainthread.codec_def = upipe_av_to_flow_def(
                    mainthread.avfctx->streams[videoStream]->codec->codec_id);
    printf("Codec flow def: %s\n", mainthread.codec_def);
    struct uref *flowdef = uref_block_flow_alloc_def_va(uref_mgr, "%s",
                                                        mainthread.codec_def);
    // build avcodec pipe
    struct upipe_mgr *upipe_avcdec_mgr = upipe_avcdec_mgr_alloc();
    assert(upipe_avcdec_mgr);
    struct upipe *avcdec = upipe_flow_alloc(upipe_avcdec_mgr,
                uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "avcdec"),
                flowdef);
    uref_free(flowdef);
    assert(avcdec);
    assert(upipe_set_ubuf_mgr(avcdec, pic_mgr));
    /* mainthread avcdec runs alone (no thread) so it doesn't need any upump_mgr
     * Please do not add one, to check the nopump (direct call) case */
    mainthread.avcdec = avcdec;

    upipe_get_flow_def(avcdec, &flowdef);

    // test pipe
    struct upipe *avcdec_test = upipe_flow_alloc(&avcdec_test_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "avcdec_test"),
            flowdef);
    assert(upipe_set_output(avcdec, avcdec_test));
    
    // null pipe
    struct upipe *nullpipe = upipe_flow_alloc(&nullpipe_mgr,
                uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "devnull"),
                flowdef);

    if (!pgm_prefix) {
        assert(upipe_set_output(avcdec, nullpipe));
    }

    // audiodec pipe
    mainthread.audiodec = NULL;
    if (audioStream >= 0) {
        mainthread.audio_def = upipe_av_to_flow_def(
                        mainthread.avfctx->streams[audioStream]->codec->codec_id);
        flowdef = uref_block_flow_alloc_def_va(uref_mgr, "%s", mainthread.audio_def),
        mainthread.audiodec = upipe_flow_alloc(upipe_avcdec_mgr,
                uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "audiodec"),
                flowdef);
        uref_free(flowdef);
        upipe_set_ubuf_mgr(mainthread.audiodec, block_mgr);
        upipe_set_uref_mgr(mainthread.audiodec, uref_mgr);
        assert(upipe_set_output(mainthread.audiodec, nullpipe));
    }


    // pthread/udeal check
    if (thread_num > 0) {
        struct thread thread[thread_num];
        printf("Allocating %d avcdec pipes\n", thread_num);
        for (i=0; i < thread_num; i++) {
            memset(&thread[i], 0, sizeof(struct thread));
            thread[i].num = i;
            thread[i].iteration = 0;
            // Init per-thread avformat
            thread[i].avfctx = NULL;
            avformat_open_input(&thread[i].avfctx, srcpath, NULL, NULL);
            assert(avformat_find_stream_info(thread[i].avfctx, NULL) >= 0);

            // Find first video stream
            thread[i].videoStream = -1;
            for (j=0; j < thread[i].avfctx->nb_streams; j++) {
                if (thread[i].avfctx->streams[j]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
                    thread[i].videoStream = j;
                    break;
                }
            }
            assert(thread[i].videoStream != -1);
            thread[i].codec_def = upipe_av_to_flow_def(
                            thread[i].avfctx->streams[videoStream]->codec->codec_id);
            flowdef = uref_block_flow_alloc_def_va(uref_mgr, "%s",
                                                   mainthread.codec_def);

            thread[i].avcdec = upipe_flow_alloc(upipe_avcdec_mgr,
                    uprobe_pfx_adhoc_alloc_va(log, UPROBE_LOG_LEVEL, "avcdec_thread(%d)", i),
                    flowdef);
            assert(thread[i].avcdec);
            assert(upipe_set_ubuf_mgr(thread[i].avcdec, pic_mgr));
            assert(upipe_set_output(thread[i].avcdec, nullpipe));

            thread[i].audio_def = NULL;
            thread[i].audiodec = NULL;
        }

        // Fire threads
        for (i=0; i < thread_num; i++) {
            assert(pthread_create(&thread[i].id, NULL, test_thread, &thread[i]) == 0);
        }
        // Join (wait for threads to exit)
        // pipes are cleaned in their respective thread
        for (i=0; i < thread_num; i++) {
            assert(!pthread_join(thread[i].id, NULL));
            avformat_close_input(&thread[i].avfctx);
        }

        printf("udeal/pthread test ended. Now launching decoding test.\n", thread_num);
    }

    // Now read with avformat
    ev_loop(loop, 0);

    // Close avformat
    avformat_close_input(&mainthread.avfctx);

    upipe_release(nullpipe);
    avcdec_test_free(avcdec_test);
    upipe_mgr_release(upipe_avcdec_mgr);
	upump_free(write_pump);

    // release managers
    upump_mgr_release(upump_mgr);
    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);
    upipe_av_clean();

    ev_default_destroy();
    return 0;
}

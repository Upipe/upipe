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
#include <upipe-av/upipe_avcodec_dec_vid.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe/upipe_helper_upipe.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "../lib/upipe-av/upipe_av_internal.h"

#define ALIVE(a) { printf("# ALIVE[%d]: %s %s - %d\n", (a), __FILE__, __func__, __LINE__); fflush(stdout); }

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
    struct upipe *avcdv;
    const char *codec_def;
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
struct uref_mgr *uref_mgr;
struct ubuf_mgr *block_mgr;
const char *pgm_prefix = NULL;

/** @internal @This fetches chroma from uref
 *  
 * @param ubuf ubuf structure
 * @param str name of the chroma
 * @param strides strides array
 * @param slices array of pointers to data plans
 * @param idx index of the chroma in slices[]/strides[]
 */
static void inline upipe_avcdv_fetch_chroma(struct ubuf *ubuf, const char *str, int *strides, uint8_t **slices, size_t idx, enum plane_action action)
{
    size_t stride = 0;
    switch(action) {

    case READ:
        ubuf_pic_plane_read(ubuf, str, 0, 0, -1, -1, (const uint8_t**)slices+idx);
        break;
    case WRITE:
        ubuf_pic_plane_write(ubuf, str, 0, 0, -1, -1, slices+idx);
        break;
    case UNMAP:
        ubuf_pic_plane_unmap(ubuf, str, 0, 0, -1, -1);
        slices[idx] = NULL;
        return;
    }
    ubuf_pic_plane_size(ubuf, str, &stride, NULL, NULL, NULL);
    strides[idx] = (int) stride;
}

/** @internal */
static void upipe_avcdv_map_frame(struct ubuf *ubuf, int *strides, uint8_t **slices, enum plane_action action)
{
    // FIXME - hardcoded chroma fetch
    upipe_avcdv_fetch_chroma(ubuf, "y8", strides, slices, 0, action);
    upipe_avcdv_fetch_chroma(ubuf, "u8", strides, slices, 1, action);
    upipe_avcdv_fetch_chroma(ubuf, "v8", strides, slices, 2, action);
}

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
            break;
        case UPROBE_AERROR:
        case UPROBE_UPUMP_ERROR:
        case UPROBE_WRITE_END:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_NEED_UBUF_MGR:
        default:
            assert(0);
            break;
    }
    return true;
}

/** phony pipe to test upipe_avcdv */
struct avcdv_test {
    struct upipe upipe;
};

/** helper phony pipe to test upipe_avcdv */
UPIPE_HELPER_UPIPE(avcdv_test, upipe);

/** helper phony pipe to test upipe_avcdv */
static struct upipe *avcdv_test_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe)
{
    struct avcdv_test *avcdv_test = malloc(sizeof(struct avcdv_test));
    if (unlikely(!avcdv_test)) return NULL;
    upipe_init(&avcdv_test->upipe, mgr, uprobe);
    upipe_throw_ready(&avcdv_test->upipe);
    return &avcdv_test->upipe;
}

/** helper phony pipe to test upipe_avcdv */
static void avcdv_test_input(struct upipe *upipe, struct uref *uref, struct upump *upump)
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

/** helper phony pipe to test upipe_avcdv */
static void avcdv_test_free(struct upipe *upipe)
{
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    upipe_throw_dead(upipe);
    struct avcdv_test *avcdv_test = avcdv_test_from_upipe(upipe);
    upipe_clean(upipe);
    free(avcdv_test);
}

/** helper phony pipe to test upipe_avcdv */
static struct upipe_mgr avcdv_test_mgr = {
    .upipe_alloc = avcdv_test_alloc,
    .upipe_input = avcdv_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

/** nullpipe (/dev/null) */
static struct upipe *nullpipe_alloc(struct upipe_mgr *mgr,
                                    struct uprobe *uprobe)
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

/** Fetch video packets using avformat and send them to avcdv pipe.
 * Also send extradata if present. */
static void fetch_av_packets(struct upump *pump)
{
    uint8_t *buf = NULL;
    AVPacket avpkt;
    struct uref *uref;
    int size;
    struct thread *thread = upump_get_opaque(pump, struct thread*);
    struct upipe *avcdv = thread->avcdv;
    assert(avcdv);

    if (thread->count < thread->limit && av_read_frame(thread->avfctx, &avpkt) >= 0) {
        if(avpkt.stream_index == videoStream) {
            size = avpkt.size;
            printf("#[%d]# Reading video frame %d - size : %d\n", thread->num, thread->count, size, avpkt.data);

            if ( !thread->extrasent && (thread->avfctx->streams[videoStream]->codec->extradata_size > 0) ) {
                thread->extrasent = upipe_avcdv_set_codec(avcdv, thread->codec_def,
                                thread->avfctx->streams[videoStream]->codec->extradata,
                                thread->avfctx->streams[videoStream]->codec->extradata_size);
            }

            // Allocate uref/ubuf_block and copy data
            uref = uref_block_alloc(uref_mgr, block_mgr, size);
            uref_block_write(uref, 0, &size, &buf);
            memcpy(buf, avpkt.data, size);
            uref_block_unmap(uref, 0);

            // Send uref to avcdv pipe and free avpkt
            upipe_input(avcdv, uref, pump);
            thread->count++;
        }
        av_free_packet(&avpkt);
    } else {
        // Send empty uref to output last frame (move to decoder ?)
        upipe_release(thread->avcdv);
        upump_stop(pump);
    }
}

static void setcodec_idler(struct upump *pump)
{
    struct thread *thread = upump_get_opaque(pump, struct thread *);
    struct upipe *avcdv = thread->avcdv;

    if (thread->iteration >= ITER_LIMIT) {
        upump_stop(pump);
        upump_start(thread->fetchav_pump);
//        upipe_release(avcdv);
        return;
    }
    if (thread->iteration >= ITER_LIMIT - 1) {
        upipe_avcdv_set_lowres(avcdv, 2);
    } else {
        upipe_avcdv_set_codec(avcdv, thread->codec_def, NULL, 0);
    }
    thread->iteration++;
}

/** Thread function from which ev_loops are launched.
 * This allows us to test avcdv/udeal */
static void *test_thread(void *_thread)
{
    struct thread *thread = ((struct thread *)_thread);
    struct upipe *avcdv = thread->avcdv;

    printf("Thread %d launched.\n", thread->num);
    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert (upump_mgr != NULL);

    assert(upipe_set_upump_mgr(avcdv, upump_mgr));

    struct upump *setcodec_pump = upump_alloc_idler(upump_mgr, setcodec_idler, thread, false);
    assert(setcodec_pump);
    assert(upump_start(setcodec_pump));

    thread->limit = THREAD_FRAMES_LIMIT;
    thread->fetchav_pump = upump_alloc_idler(upump_mgr, fetch_av_packets, thread, true);
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
    fprintf(stdout, "Usage: %s <source file>\n", argv0);
    exit(EXIT_FAILURE);
}

int main (int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    if (argc < 2) {
        usage(argv[0]);
    }
    const char *srcpath = argv[1];
    if (argc > 2) {
        pgm_prefix = argv[2];
    }

    int i, j;
    AVDictionary *options = NULL;
    struct thread thread[THREAD_NUM];

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
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);
    assert(upump_mgr != NULL);
    struct thread mainthread;
    struct upump *write_pump = upump_alloc_idler(upump_mgr, fetch_av_packets, &mainthread, false);
    assert(write_pump);
    assert(upump_start(write_pump));

    // main thread description
    memset(&mainthread, 0, sizeof(struct thread));
    mainthread.limit = FRAMES_LIMIT;
    mainthread.num = -1;

    // build avcodec pipe
    assert(upipe_av_init(false));
    struct upipe_mgr *upipe_avcdv_mgr = upipe_avcdv_mgr_alloc();
    assert(upipe_avcdv_mgr);
    struct upipe *avcdv = upipe_alloc(upipe_avcdv_mgr, uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "avcdv"));
    assert(avcdv);
    assert(upipe_set_ubuf_mgr(avcdv, pic_mgr));
    assert(upipe_set_uref_mgr(avcdv, uref_mgr));
    /* mainthread avcdv runs alone (no thread) so it doesn't need any upump_mgr
     * Please do not add one, to check the nopump (direct call) case */
    mainthread.avcdv = avcdv;

    // test pipe
    struct upipe *avcdv_test = upipe_alloc(&avcdv_test_mgr, uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "avcdv_test"));
    assert(upipe_set_output(avcdv, avcdv_test));
    
    // null pipe
    struct upipe *nullpipe = upipe_alloc(&nullpipe_mgr, uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "devnull"));

    if (!pgm_prefix) {
        assert(upipe_set_output(avcdv, nullpipe));
    }

    // Open file with avformat
    printf("Trying to open %s ...\n", srcpath);
    avformat_open_input(&mainthread.avfctx, srcpath, NULL, NULL);
    assert(mainthread.avfctx);
    assert(avformat_find_stream_info(mainthread.avfctx, NULL) >= 0);
    av_dump_format(mainthread.avfctx, 0, srcpath, 0);

    // Find first video stream
    videoStream = -1;
    for (i=0; i < mainthread.avfctx->nb_streams; i++) {
        if (mainthread.avfctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    assert(videoStream != -1);

    // set codec def and test _context()/_release()
    mainthread.codec_def = upipe_av_to_flow_def(mainthread.avfctx->streams[videoStream]->codec->codec_id);
    printf("Codec flow def: %s\n", mainthread.codec_def);
    struct uref *flowdef = uref_block_flow_alloc_def_va(uref_mgr, "%s", mainthread.codec_def);
    upipe_use(avcdv);
    upipe_input(avcdv, flowdef, NULL);
    upipe_release(avcdv);

    // Check codec def back
    const char *codec_def;
    assert(upipe_avcdv_get_codec(avcdv, &codec_def));
    assert(!strcmp(codec_def, mainthread.codec_def));
    printf("upipe_avcdv_get_codec: %s\n", mainthread.codec_def);

    // pthread/udeal check
    printf("Allocating %d avcdv pipes\n", THREAD_NUM);
    for (i=0; i < THREAD_NUM; i++) {
        memset(&thread[i], 0, sizeof(struct thread));
        thread[i].num = i;
        thread[i].iteration = 0;
        thread[i].avcdv = upipe_alloc(upipe_avcdv_mgr,
                        uprobe_pfx_adhoc_alloc_va(log, UPROBE_LOG_LEVEL, "avcdv_thread(%d)", i));
        assert(thread[i].avcdv);
        assert(upipe_set_ubuf_mgr(thread[i].avcdv, pic_mgr));
        assert(upipe_set_uref_mgr(thread[i].avcdv, uref_mgr));
        assert(upipe_set_output(thread[i].avcdv, nullpipe));

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
        thread[i].codec_def = upipe_av_to_flow_def(thread[i].avfctx->streams[videoStream]->codec->codec_id);
    }

    // Fire threads
    for (i=0; i < THREAD_NUM; i++) {
        assert(pthread_create(&thread[i].id, NULL, test_thread, &thread[i]) == 0);
    }
    // Join (wait for threads to exit)
    // pipes are cleaned in their respective thread
    for (i=0; i < THREAD_NUM; i++) {
        assert(!pthread_join(thread[i].id, NULL));
        avformat_close_input(&thread[i].avfctx);
    }

    printf("udeal/pthread test ended. Now launching decoding test.\n", THREAD_NUM);

    // Now read with avformat
    ev_loop(loop, 0);

    // Close avformat
    avformat_close_input(&mainthread.avfctx);

    upipe_release(nullpipe);
    avcdv_test_free(avcdv_test);
    upipe_mgr_release(upipe_avcdv_mgr);
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

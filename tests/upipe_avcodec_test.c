/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen <bencoh@notk.org>
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
 * @short unit tests for upipe_avcodec encode/decode pipes
 */

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/upipe.h>
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
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe-modules/upipe_null.h>

#undef NDEBUG

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <ev.h>
#include <pthread.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define THREAD_NUM          4
#define FRAMES_LIMIT        100
#define SETCODEC_LIMIT      100
#define THREAD_FRAMES_LIMIT (FRAMES_LIMIT / 8)

struct upipe_mgr *upipe_avcdec_mgr;
struct upipe_mgr *upipe_avcenc_mgr;
struct upipe_mgr *upipe_null_mgr;
struct uref_mgr *uref_mgr;
struct ubuf_mgr *block_mgr;
struct ubuf_mgr *pic_mgr;
struct uprobe *logger;

struct thread {
    pthread_t id;
    unsigned int num;
    unsigned int iteration;
    unsigned int limit;
    struct upipe *avcenc;
    struct upump *source;
};

/** definition of our uprobe */
bool catch(struct uprobe *uprobe, struct upipe *upipe, enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEED_INPUT:
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

/* fill picture with some stuff */
void fill_pic(struct ubuf *ubuf)
{
    const char *chroma = NULL;
    uint8_t *buf, hsub, vsub;
    size_t stride, width, height;
    int i, j;
    
    ubuf_pic_size(ubuf, &width, &height, NULL);
    while (ubuf_pic_plane_iterate(ubuf, &chroma) && chroma) {
        ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &buf);
        ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub, NULL);
        for (j = 0; j < height/vsub; j++) {
            for (i=0; i < width/hsub; i++) {
                buf[i] = 2*i + j;
            }
            buf += stride;
        }
        ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1);
    }
}

/* build video pipeline */
struct upipe *build_pipeline(const char *codec_def,
                             struct upump_mgr *upump_mgr, int num)
{
    /* encoder */
    struct upipe *avcenc = upipe_alloc(upipe_avcenc_mgr,
        uprobe_pfx_adhoc_alloc_va(logger, UPROBE_LOG_LEVEL, "avcenc %d", num));
    assert(avcenc);
    assert(upipe_set_ubuf_mgr(avcenc, block_mgr));
    assert(upipe_set_uref_mgr(avcenc, uref_mgr));
    assert(upipe_avcenc_set_codec(avcenc, codec_def));
    if (upump_mgr) {
        assert(upipe_set_upump_mgr(avcenc, upump_mgr));
    }

    /* decoder */
    struct upipe *avcdec = upipe_alloc(upipe_avcdec_mgr,
        uprobe_pfx_adhoc_alloc_va(logger, UPROBE_LOG_LEVEL, "avcdec %d", num));
    assert(avcdec);
    assert(upipe_set_ubuf_mgr(avcdec, pic_mgr));
    assert(upipe_set_uref_mgr(avcdec, uref_mgr));
    if (upump_mgr) {
        assert(upipe_set_upump_mgr(avcdec, upump_mgr));
    }
    assert(upipe_set_output(avcenc, avcdec));
    upipe_release(avcdec);

    /* /dev/null */
    struct upipe *null = upipe_alloc(upipe_null_mgr,
        uprobe_pfx_adhoc_alloc_va(logger, UPROBE_LOG_LEVEL, "null %d", num));
    assert(null);
    upipe_set_output(avcdec, null);
    upipe_release(null);

    return avcenc;
}

/* picture generator */
void source_idler(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread*);
    struct upipe *avcenc = thread->avcenc;
    struct uref *pic;

    pic = uref_pic_alloc(uref_mgr, pic_mgr, 64, 48);
    fill_pic(pic->ubuf);
    upipe_input(avcenc, pic, upump);

    if (thread->iteration > thread->limit) {
        upipe_release(thread->avcenc);
        upump_stop(upump);
        return;
    }
    thread->iteration++;
}

/* set codec */
void setcodec_idler(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread*);
    struct upipe *avcenc = thread->avcenc;

    if (thread->iteration > thread->limit) {
        /* enough played with set_codec(), start source */
        thread->iteration = 0;
        upump_stop(upump);
        struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
        upipe_input(thread->avcenc, flow, thread->source);
        upump_start(thread->source);
        return;
    }

    upipe_avcenc_set_codec(avcenc, "mpeg2video.pic.");
    thread->iteration++;
}

/* thread entry point */
void *thread_start(void *_thread)
{
    struct thread *thread = _thread;

    printf("Thread %d launched.\n", thread->num);

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop);

    thread->avcenc = build_pipeline("mpeg2video.pic.", upump_mgr, thread->num);
    thread->limit = SETCODEC_LIMIT;

    thread->source = upump_alloc_idler(upump_mgr, source_idler, thread, true);
    struct upump *setcodec_pump = upump_alloc_idler(upump_mgr, setcodec_idler,
                                                    thread, false);
    upump_start(setcodec_pump);

    ev_loop(loop, 0);

    printf("Thread %d ended.\n", thread->num);
    upump_free(thread->source);
    upump_free(setcodec_pump);
    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

int main(int argc, char **argv)
{
    struct uref *pic;
    int i;
    int opt;
    int thread_num = THREAD_NUM;

    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    while ((opt = getopt(argc, argv, "n:")) != -1) {
        switch(opt) {
            case 'n':
                thread_num = strtod(optarg, NULL);
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }

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
    pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
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
    logger = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    /* init upipe_av */
    assert(upipe_av_init(false));

    /* global managers */
    assert(upipe_avcdec_mgr = upipe_avcdec_mgr_alloc());
    assert(upipe_avcenc_mgr = upipe_avcenc_mgr_alloc());
    assert(upipe_null_mgr = upipe_null_mgr_alloc());

    /* multi-threaded test with upump_mgr */
    if (thread_num > 0) {
        struct thread thread[thread_num];
        for (i=0; i < thread_num; i++) {
            memset(&thread[i], 0, sizeof(struct thread));
            thread[i].num = i;
            assert(pthread_create(&thread[i].id, NULL, thread_start, &thread[i]) == 0);
        }
        for (i=0; i < thread_num; i++) {
            assert(!pthread_join(thread[i].id, NULL));
        }
        printf("Multi-threaded test ended. Start monothread\n");
    }

    /* mono-threaded test without upump_mgr */
    struct upipe *avcenc = build_pipeline("mpeg2video.pic.", NULL, -1);
    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    upipe_input(avcenc, flow, NULL);

    for (i=0; i < FRAMES_LIMIT; i++) {
        pic = uref_pic_alloc(uref_mgr, pic_mgr, 120, 96);
        fill_pic(pic->ubuf);
        upipe_input(avcenc, pic, NULL);
   }

    flow = uref_alloc(uref_mgr);
    uref_flow_set_end(flow);
    upipe_input(avcenc, flow, NULL);
    upipe_release(avcenc);
    printf("Everything good so far, cleaning\n");

    /* clean managers and probes */
    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_log_free(logger);
    uprobe_stdio_free(uprobe_stdio);
    upipe_av_clean();

    return 0;
}

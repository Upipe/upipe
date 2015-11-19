/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
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
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_decode.h>
#include <upipe-av/upipe_avcodec_encode.h>
#include <upipe-modules/upipe_null.h>

#undef NDEBUG

UREF_ATTR_INT(xflow, num, "x.f.num", flow num)

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <ev.h>
#include <pthread.h>

#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define THREAD_NUM          4
#define FRAMES_LIMIT        100
#define THREAD_FRAMES_LIMIT (FRAMES_LIMIT / 8)
#define WIDTH 120
#define HEIGHT 90
#define STREAM stdout

enum uprobe_log_level loglevel = UPROBE_LOG_VERBOSE;

struct upipe_mgr *upipe_avcdec_mgr;
struct upipe_mgr *upipe_avcenc_mgr;
struct upipe_mgr *upipe_null_mgr;
struct uref_mgr *uref_mgr;
struct ubuf_mgr *sound_mgr;
struct ubuf_mgr *pic_mgr;
struct uprobe *logger;
struct uprobe uprobe_avcenc_s;

struct thread {
    pthread_t id;
    unsigned int num;
    unsigned int iteration;
    unsigned int limit;
    struct upipe *avcenc;
};

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_NEED_UPUMP_MGR:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

/** definition of our uprobe */
static int catch_avcenc(struct uprobe *uprobe, struct upipe *upipe,
                        int event, va_list args)
{
    struct uref *flow = NULL;
    const char *def;
    int64_t num = 0;
    struct upump_mgr *upump_mgr = NULL;

    if (event != UPROBE_NEED_OUTPUT) {
        return uprobe_throw_next(uprobe, upipe, event, args);
    }

    upipe_get_flow_def(upipe, &flow);
    assert(flow != NULL);
    uref_xflow_get_num(flow, &num);
    uref_flow_get_def(flow, &def);
    assert(def != NULL);

    /* decoder lives in encoder's thread */
    upump_mgr = upipe_get_opaque(upipe, struct upump_mgr *);

    /* decoder */
    struct upipe *avcdec = upipe_void_alloc_output(upipe, upipe_avcdec_mgr,
        uprobe_upump_mgr_alloc(
            uprobe_pfx_alloc_va(uprobe_use(logger), loglevel,
                                "avcdec %"PRId64, num), upump_mgr));
    assert(avcdec);
    upipe_release(avcdec);

    /* /dev/null */
    struct upipe *null = upipe_void_alloc(upipe_null_mgr,
        uprobe_pfx_alloc_va(uprobe_use(logger), loglevel,
                            "null %"PRId64, num));
    assert(null);
    upipe_null_dump_dict(null, true);
    upipe_set_output(avcdec, null);
    upipe_release(null);
    return UBASE_ERR_NONE;
}

/* fill picture with some stuff */
void fill_pic(struct ubuf *ubuf)
{
    const char *chroma = NULL;
    uint8_t *buf, hsub, vsub;
    size_t stride, width, height;
    int i, j;
    
    ubuf_pic_size(ubuf, &width, &height, NULL);
    while (ubase_check(ubuf_pic_plane_iterate(ubuf, &chroma)) && chroma) {
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

/* build video or audio pipeline */
struct upipe *build_pipeline(const char *codec_def,
                             struct upump_mgr *upump_mgr, int num,
                             struct uref *flow_def)
{
    struct uref *output_flow = uref_dup(flow_def);
    ubase_assert(uref_flow_set_def_va(output_flow, "block.%s", codec_def));
    assert(flow_def != NULL);
    uref_xflow_set_num(flow_def, num);

    /* encoder */
    struct upipe *avcenc = upipe_flow_alloc(upipe_avcenc_mgr,
        uprobe_upump_mgr_alloc(
            uprobe_pfx_alloc_va(&uprobe_avcenc_s,
                                loglevel, "avcenc %d", num), upump_mgr),
        output_flow);
    uref_free(output_flow);
    assert(avcenc);
    ubase_assert(upipe_set_flow_def(avcenc, flow_def));
    upipe_set_opaque(avcenc, upump_mgr);

    return avcenc;
}

/* picture generator */
void source_idler(struct upump *upump)
{
    struct thread *thread = upump_get_opaque(upump, struct thread*);
    struct upipe *avcenc = thread->avcenc;
    struct uref *pic;

    pic = uref_pic_alloc(uref_mgr, pic_mgr, WIDTH, HEIGHT);
    fill_pic(pic->ubuf);
    upipe_input(avcenc, pic, &upump);

    if (thread->iteration > thread->limit) {
        upipe_release(thread->avcenc);
        upump_stop(upump);
        return;
    }
    thread->iteration++;
}

/* thread entry point */
void *thread_start(void *_thread)
{
    struct thread *thread = _thread;

    fprintf(STREAM, "Thread %d launched.\n", thread->num);

    struct ev_loop *loop = ev_loop_new(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);

    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow != NULL);
    ubase_assert(uref_pic_flow_add_plane(flow, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow, WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(flow, HEIGHT));
    struct urational fps = { .num = 25, .den = 1 };
    ubase_assert(uref_pic_flow_set_fps(flow, fps));
    thread->avcenc = build_pipeline("mpeg2video.pic.", upump_mgr, thread->num,
                                    flow);
    uref_free(flow);
    thread->limit = FRAMES_LIMIT;

    struct upump *source = upump_alloc_idler(upump_mgr, source_idler, thread,
                                             NULL);
    upump_start(source);

    ev_loop(loop, 0);

    printf("Thread %d ended.\n", thread->num);
    assert(thread->iteration > thread->limit);
    upump_free(source);
    upump_mgr_release(upump_mgr);
    ev_loop_destroy(loop);

    return NULL;
}

int main(int argc, char **argv)
{
    struct uref *pic, *sound;
    int i;
    int opt;
    int thread_num = THREAD_NUM;

    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    while ((opt = getopt(argc, argv, "dhn:")) != -1) {
        switch(opt) {
            case 'd':
                loglevel = UPROBE_LOG_VERBOSE;
                break;
            case 'n':
                thread_num = strtod(optarg, NULL);
                break;
            case 'h':
                printf("Usage: %s [-n <threads>]\n", argv[0]);
                exit(0);
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

    /* sound */
    sound_mgr = ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, 4, 32);
    assert(sound_mgr);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(sound_mgr, "lr"));

    /* planar YUV (I420) */
    pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    assert(pic_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "v8", 2, 2, 1));

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    logger = uprobe_stdio_alloc(&uprobe, STREAM, loglevel);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    uprobe_init(&uprobe_avcenc_s, catch_avcenc, uprobe_use(logger));


    /* init upipe_av */
    assert(upipe_av_init(false, uprobe_use(logger)));

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
    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow != NULL);
    ubase_assert(uref_pic_flow_add_plane(flow, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow, WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(flow, HEIGHT));
    struct urational fps = { .num = 25, .den = 1 };
    ubase_assert(uref_pic_flow_set_fps(flow, fps));
    struct upipe *avcenc = build_pipeline("mpeg2video.pic.", NULL, -1, flow);
    uref_free(flow);

    for (i=0; i < FRAMES_LIMIT; i++) {
        pic = uref_pic_alloc(uref_mgr, pic_mgr, WIDTH, HEIGHT);
        assert(pic != NULL);
        fill_pic(pic->ubuf);
        upipe_input(avcenc, pic, NULL);
   }

    upipe_release(avcenc);
    printf("Everything good so far, cleaning\n");

    /* mono-threaded audio test without upump_mgr */
    flow = uref_sound_flow_alloc_def(uref_mgr, "s16le.", 2, 4);
    assert(flow != NULL);
    ubase_assert(uref_sound_flow_add_plane(flow, "lr"));
    ubase_assert(uref_sound_flow_set_channels(flow, 2));
    ubase_assert(uref_sound_flow_set_rate(flow, 48000));
    avcenc = build_pipeline("mp2.sound.", NULL, -1, flow);
    uref_free(flow);

    for (i=0; i < FRAMES_LIMIT; i++) {
        uint8_t *buf = NULL;
        int samples = (1024+i-FRAMES_LIMIT/2);
        sound = uref_sound_alloc(uref_mgr, sound_mgr, samples);
        assert(sound != NULL);
        ubase_assert(uref_sound_plane_write_uint8_t(sound, "lr", 0, -1, &buf));
        memset(buf, 0, 2*2*samples);
        ubase_assert(uref_sound_plane_unmap(sound, "lr", 0, -1));
        upipe_input(avcenc, sound, NULL);
    }

    upipe_release(avcenc);
    printf("Everything good so far, cleaning\n");

    /* clean managers and probes */
    upipe_mgr_release(upipe_avcdec_mgr);
    upipe_mgr_release(upipe_avcenc_mgr);
    upipe_mgr_release(upipe_null_mgr);
    ubuf_mgr_release(sound_mgr);
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    upipe_av_clean();
    uprobe_release(logger);
    uprobe_clean(&uprobe_avcenc_s);
    uprobe_clean(&uprobe);

    return 0;
}

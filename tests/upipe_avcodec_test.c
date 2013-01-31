/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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

#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
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
#include <upipe-av/upipe_av.h>
#include <upipe-av/upipe_avcodec_dec_vid.h>

#include <upipe/upipe_helper_upipe.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "../lib/upipe-av/upipe_av_internal.h"

#define ALIVE() { printf("# ALIVE: %s %s - %d\n", __FILE__, __func__, __LINE__); fflush(stdout); } // FIXME - debug - remove this

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0
#define ULOG_LEVEL ULOG_DEBUG

#define FLOW_NAME           "fooflow"

/** @internal
 */
enum plane_action {
    UNMAP,
    READ,
    WRITE
};

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

/** @internal
 */
static void upipe_avcdv_map_frame(struct ubuf *ubuf, int *strides, uint8_t **slices, enum plane_action action)
{
    // FIXME - hardcoded chroma fetch
    upipe_avcdv_fetch_chroma(ubuf, "y8", strides, slices, 0, action);
    upipe_avcdv_fetch_chroma(ubuf, "u8", strides, slices, 1, action);
    upipe_avcdv_fetch_chroma(ubuf, "v8", strides, slices, 2, action);
}

/* Save picture to pgm file */
static void pgm_save(const uint8_t *buf, int wrap, int xsize, int ysize, int num) // FIXME debug
{
    char filename[256];
    FILE *f;
    int i;
    snprintf(filename, sizeof(filename), "%04d.pgm", num);
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

/** phony pipe to test upipe_genaux */
struct avcdv_test {
    struct uref *uref;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_genaux */
UPIPE_HELPER_UPIPE(avcdv_test, upipe);

/** helper phony pipe to test upipe_genaux */
static struct upipe *avcdv_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe, struct ulog *ulog)
{
    struct avcdv_test *avcdv_test = malloc(sizeof(struct avcdv_test));
    if (unlikely(!avcdv_test)) return NULL;
    upipe_init(&avcdv_test->upipe, mgr, uprobe, ulog);
    return &avcdv_test->upipe;
}

/** helper phony pipe to test upipe_genaux */
static bool avcdv_test_control(struct upipe *upipe, enum upipe_command command, va_list args)
{
    return false;
}

static void avcdv_test_input(struct upipe *upipe, struct uref *uref, struct upump *upump)
{
    const uint8_t *buf = NULL;
    size_t stride = 0, hsize = 0, vsize = 0;
    static int counter = 0;

    assert(uref != NULL);
    ulog_debug(upipe->ulog, "===> received input uref");
    udict_dump(uref->udict, upipe->ulog);
    if (uref->ubuf) {
        uref_pic_plane_read(uref, "y8", 0, 0, -1, -1, &buf);
        uref_pic_plane_size(uref, "y8", &stride, NULL, NULL, NULL);
        uref_pic_size(uref, &hsize, &vsize, NULL);
        pgm_save(buf, stride, hsize, vsize, counter);
        uref_pic_plane_unmap(uref, "y8", 0, 0, -1, -1);
        counter++;
    }
    uref_free(uref);
    // FIXME peek into buffer
}

/** helper phony pipe to test upipe_genaux */
static void avcdv_test_free(struct upipe *upipe)
{
    ulog_debug(upipe->ulog, "releasing pipe %p", upipe);
    struct avcdv_test *avcdv_test = avcdv_test_from_upipe(upipe);
    upipe_clean(upipe);
    free(avcdv_test);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr avcdv_test_mgr = {
    .upipe_alloc = avcdv_test_alloc,
    .upipe_input = avcdv_test_input,
    .upipe_control = avcdv_test_control,
    .upipe_release = NULL,
    .upipe_use = NULL,

    .upipe_mgr_release = NULL
};



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
    char *srcpath = argv[1];
    struct uref *uref;

    AVFormatContext *avfctx = NULL;
    AVDictionary *options = NULL;
    AVPacket avpkt;
    int videoStream, i, size;
    uint8_t *buf = NULL;
    const char *codec_def;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    struct ulog *mainlog = ulog_stdio_alloc(stdout, ULOG_LEVEL, "main");

    /* block */
    struct ubuf_mgr *block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
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
    struct uprobe *uprobe_log = uprobe_log_alloc(&uprobe, ULOG_DEBUG);
    assert(uprobe_log != NULL);

    // build avcodec pipe
    assert(upipe_av_init(false));
    struct upipe_mgr *upipe_avcdv_mgr = upipe_avcdv_mgr_alloc();
    assert(upipe_avcdv_mgr);
    struct upipe *avcdv = upipe_alloc(upipe_avcdv_mgr, uprobe_log, ulog_stdio_alloc(stdout, ULOG_LEVEL, "avcdv"));
    assert(avcdv);
    assert(upipe_set_ubuf_mgr(avcdv, pic_mgr));
    assert(upipe_set_uref_mgr(avcdv, uref_mgr));

    // test pipe
    struct upipe *avcdv_test = upipe_alloc(&avcdv_test_mgr, uprobe_log, ulog_stdio_alloc(stdout, ULOG_LEVEL, "avcdv_test"));
    assert(upipe_set_output(avcdv, avcdv_test));

    // Open file with avformat
    printf("Trying to open %s ...\n", srcpath);
    avformat_open_input(&avfctx, srcpath, NULL, NULL);
    assert(avformat_find_stream_info(avfctx, NULL) >= 0);
    av_dump_format(avfctx, 0, srcpath, 0);

    // Find first video stream
    videoStream = -1;
    for (i=0; i < avfctx->nb_streams; i++) {
        if (avfctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    assert(videoStream != -1);
    codec_def = upipe_av_to_flow_def(avfctx->streams[videoStream]->codec->codec_id);
    printf("Codec flow def: %s\n", codec_def);
    assert(upipe_avcdv_set_codec(avcdv, codec_def));

    // Now read with avformat
    i = 0;
    bool extrasent = false;
    while (av_read_frame(avfctx, &avpkt) >= 0) {
        if(avpkt.stream_index == videoStream) {
            size = avpkt.size;
            printf("# Reading video frame %d - size : %d\n", i, size, avpkt.data);

            if ( !extrasent && (avfctx->streams[videoStream]->codec->extradata_size > 0) ) {
                extrasent = upipe_avcdv_set_extradata(avcdv, avfctx->streams[videoStream]->codec->extradata, avfctx->streams[videoStream]->codec->extradata_size);
            }

            // Allocate uref/ubuf_block and copy data
            uref = uref_block_alloc(uref_mgr, block_mgr, size);
            uref_block_write(uref, 0, &size, &buf);
            memcpy(buf, avpkt.data, size);
            uref_block_unmap(uref, 0, size);

            // Send uref to avcdv pipe and free avpkt
            upipe_input(avcdv, uref, NULL);
            i++;
        }
        av_free_packet(&avpkt);
    }
    // Send empty uref to output last frame (move to decoder ?)
    upipe_input(avcdv, uref_block_alloc(uref_mgr, block_mgr, 0), NULL);

    // Close avformat
    avformat_close_input(&avfctx);

    upipe_release(avcdv);
    avcdv_test_free(avcdv_test);
    upipe_mgr_release(upipe_avcdv_mgr);
    // release managers
    ubuf_mgr_release(block_mgr);
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_log_free(uprobe_log);
    ulog_free(mainlog);
    upipe_av_clean();
    return 0;
}

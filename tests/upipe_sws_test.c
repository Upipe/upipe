/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short unit tests for swscale pipes
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
#include <upipe/udict_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_std.h>
#include <upipe-swscale/upipe_sws.h>

#include <upipe/upipe_helper_upipe.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h> // debug

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define SRCSIZE             32
#define DSTSIZE             16

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
    }
    return true;
}

enum plane_action {
    UNMAP,
    READ,
    WRITE
};

/** fetches a single chroma into slices and set corresponding stride */
static void inline fetch_chroma(struct uref *uref, const char *str, int *strides, uint8_t **slices, size_t idx, enum plane_action action)
{
    size_t stride = 0;
    switch(action) {

    case READ:
        uref_pic_plane_read(uref, str, 0, 0, -1, -1, (const uint8_t**)slices+idx);
        break;
    case WRITE:
        uref_pic_plane_write(uref, str, 0, 0, -1, -1, slices+idx);
        break;
    case UNMAP:
        uref_pic_plane_unmap(uref, str, 0, 0, -1, -1);
        return;
    }
    uref_pic_plane_size(uref, str, &stride, NULL, NULL, NULL);
    strides[idx] = (int) stride;
}

static void filldata(struct uref *uref, int *strides, uint8_t **slices, enum plane_action action)
{
    fetch_chroma(uref, "y8", strides, slices, 0, action);
    fetch_chroma(uref, "u8", strides, slices, 1, action);
    fetch_chroma(uref, "v8", strides, slices, 2, action);
}

/* fill picture with some reference */
static void fill_in(struct uref *uref,
                    const char *chroma, uint8_t hsub, uint8_t vsub,
                    uint8_t macropixel_size)
{
    size_t hsize, vsize, stride;
    uint8_t *buffer;
    uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buffer);
    uref_pic_plane_size(uref, chroma, &stride, NULL, NULL, NULL);
    assert(buffer != NULL);
    uref_pic_size(uref, &hsize, &vsize, NULL);
    hsize /= hsub;
    hsize *= macropixel_size;
    vsize /= vsub;
    for (int y = 0; y < vsize; y++) {
        for (int x = 0; x < hsize; x++)
            buffer[x] = 1 + (y * hsize) + x;
        buffer += stride;
    }
    uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
}

/* compare a chroma of two pictures */
static bool compare_chroma(struct uref **urefs, const char *chroma, uint8_t hsub, uint8_t vsub, uint8_t macropixel_size, struct uprobe *uprobe)
{
    char string[512], *str;
    size_t hsize[2], vsize[2];
    int stride[2];
    uint8_t *buffer[2];
    int i, x, y;

    assert(urefs);
    assert(chroma);
    assert(uprobe);
    assert(hsub);
    assert(vsub);
    assert(macropixel_size);

    uprobe_dbg_va(uprobe, NULL, "comparing %p and %p - chroma %s - %"PRIu8" %"PRIu8" %"PRIu8, urefs[0], urefs[1], chroma, hsub, vsub, macropixel_size);
    for (i = 0; i < 2; i++)
    {
        assert(urefs[i]);
//        uref_dump(urefs[i], uprobe);
        fetch_chroma(urefs[i], chroma, stride, buffer, i, READ);
        assert(buffer[i]);
        uref_pic_size(urefs[i], &hsize[i], &vsize[i], NULL);
        hsize[i] /= hsub;
        hsize[i] *= macropixel_size;
        vsize[i] /= vsub;
    }

    assert(vsize[0] == vsize[1]);
    assert(hsize[0] == hsize[1]);
    assert(stride[0] == stride[1]);
    for (y = 0; y < vsize[0]; y++) {
        str = string;
        for (x = 0; x < hsize[0]; x++) {
            if (buffer[0][x] != buffer[1][x]) {
                uprobe_dbg_va(uprobe, NULL, "####### Pos %d %d differs: %"PRIu8" - %"PRIu8" !", x, y);
                return false;
            }
            str += snprintf(str, 5, "%02"PRIx8" ", buffer[0][x]);
        }
        buffer[0] += stride[0];
        buffer[1] += stride[1];
        uprobe_dbg(uprobe, NULL, string);
    }

    uprobe_dbg_va(uprobe, NULL, "Yay, same pics for %s", chroma);
    for (i=0; i < 2; i++) {
        fetch_chroma(urefs[i], chroma, stride,  buffer, i, UNMAP);
    }
    return true;
}

/** helper phony pipe to test upipe_sws */
struct sws_test {
    struct uref *flow;
    struct uref *pic;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_sws */
UPIPE_HELPER_UPIPE(sws_test, upipe);

/** helper phony pipe to test upipe_sws */
static struct upipe *sws_test_alloc(struct upipe_mgr *mgr,
                                    struct uprobe *uprobe)
{
    struct sws_test *sws_test = malloc(sizeof(struct sws_test));
    assert(sws_test != NULL);
    sws_test->flow = NULL;
    sws_test->pic = NULL;
	upipe_init(&sws_test->upipe, mgr, uprobe);
    upipe_throw_ready(&sws_test->upipe);
    return &sws_test->upipe;
}

/** helper phony pipe to test upipe_sws */
static void sws_test_input(struct upipe *upipe, struct uref *uref,
                           struct upump *upump)
{
    struct sws_test *sws_test = sws_test_from_upipe(upipe);
    const char *def;
    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");

    if (unlikely(uref_flow_get_def(uref, &def))) {
        assert(def);
        if (sws_test->flow) {
            uref_free(sws_test->flow);
            sws_test->flow = NULL;
        }
        sws_test->flow = uref;
        upipe_dbg_va(upipe, "flow def %s", def);
        return;
    }
    if (sws_test->pic) {
        uref_free(sws_test->pic);
        sws_test->pic = NULL;
    }
    sws_test->pic = uref;
    upipe_dbg(upipe, "received pic");
}

/** helper phony pipe to test upipe_sws */
static void sws_test_free(struct upipe *upipe)
{
    upipe_dbg(upipe, "releasing pipe");
    upipe_throw_dead(upipe);
    struct sws_test *sws_test = sws_test_from_upipe(upipe);
    if (sws_test->pic) uref_free(sws_test->pic);
    if (sws_test->flow) uref_free(sws_test->flow);
    upipe_clean(upipe);
    free(sws_test);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr sws_test_mgr = {
    .upipe_alloc = sws_test_alloc,
    .upipe_input = sws_test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

// DEBUG - from swscale/swscale_unscaled.c
static int check_image_pointers(const uint8_t * const data[4], enum PixelFormat pix_fmt, const int linesizes[4])
{
    const AVPixFmtDescriptor *desc = &av_pix_fmt_descriptors[pix_fmt];
    int i;

    for (i = 0; i < 4; i++) {
        int plane = desc->comp[i].plane;
        printf("Plane %d(%d): d: %p - l: %u\n", i, plane, data[plane], linesizes[plane]);
        if (!data[plane] || !linesizes[plane])
        {
            printf("Something's fishy\n");
            return 0;
        }
    }

    return 1;
}

int main(int argc, char **argv)
{

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr;
    struct uref *pic_flow, *uref1, *uref2;
    int strides[4], dstrides[4];
    uint8_t *slices[4], *dslices[4];
    int ret;

    struct SwsContext *img_convert_ctx;

    /* planar I420 */
    ubuf_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(ubuf_mgr != NULL);
    assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "y8", 1, 1, 1));
    assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "u8", 2, 2, 1));
    assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "v8", 2, 2, 1));

    pic_flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(pic_flow != NULL);
    assert(uref_pic_flow_add_plane(pic_flow, 1, 1, 1, "y8"));
    assert(uref_pic_flow_add_plane(pic_flow, 2, 2, 1, "u8"));
    assert(uref_pic_flow_add_plane(pic_flow, 2, 2, 1, "v8"));
//    uref_dump(pic_flow, mainlog);

    /* try allocating */
    assert(uref_pic_alloc(uref_mgr, ubuf_mgr, 31, 32) == NULL);
    assert(uref_pic_alloc(uref_mgr, ubuf_mgr, 32, 31) == NULL);

    /* allocate reference picture */
    uref1 = uref_pic_alloc(uref_mgr, ubuf_mgr, SRCSIZE, SRCSIZE);
    assert(uref1 != NULL);
    assert(uref1->ubuf != NULL);

    /* fill reference picture */
    fill_in(uref1, "y8", 1, 1, 1);
    fill_in(uref1, "u8", 2, 2, 1);
    fill_in(uref1, "v8", 2, 2, 1);
//    uref_dump(uref1, mainlog);

    // sws_scale test
    // uref2 : dest image
    uref2 = uref_pic_alloc(uref_mgr, ubuf_mgr, DSTSIZE, DSTSIZE);
    assert(uref2);
    assert(uref2->ubuf);

    img_convert_ctx = sws_getCachedContext(NULL, SRCSIZE, SRCSIZE, PIX_FMT_YUV420P, DSTSIZE, DSTSIZE, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
    assert(img_convert_ctx);

    filldata(uref1, strides, slices, READ);
    filldata(uref2, dstrides, dslices, WRITE);

    assert(slices[0]);
    assert(slices[1]);
    assert(slices[2]);
    assert(strides[0]);
    assert(strides[1]);
    assert(strides[2]);

    assert(check_image_pointers((const uint8_t * const*) slices, PIX_FMT_YUV420P, strides));
    assert(check_image_pointers((const uint8_t * const*) dslices, PIX_FMT_YUV420P, dstrides));

    // fire raw swscale test
    ret = sws_scale(img_convert_ctx, (const uint8_t * const*) slices, strides, 0, SRCSIZE, dslices, dstrides);
    sws_freeContext(img_convert_ctx);

    filldata(uref1, strides, slices, UNMAP);
    filldata(uref2, dstrides, dslices, UNMAP);

    /*
     * now test upipe_sws module
     */

    /* build sws pipe and dependencies */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);
    struct upipe_mgr *upipe_sws_mgr = upipe_sws_mgr_alloc();
    assert(upipe_sws_mgr != NULL);
    struct upipe *sws = upipe_alloc(upipe_sws_mgr, uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "sws")); 
    assert(sws != NULL);
    assert(upipe_set_ubuf_mgr(sws, ubuf_mgr));

    /* build phony pipe */
    struct upipe *sws_test = upipe_alloc(&sws_test_mgr, uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "sws_test"));
    uprobe_dbg_va(log, NULL, "Pipe addr: sws:\t %p", sws);
    uprobe_dbg_va(log, NULL, "Pipe addr: sws_test: %p", sws_test);
    assert(sws_test);

    /* connect upipe_sws output to sws_test */
    assert(upipe_set_output(sws, sws_test));

    /* Send first flow definition packet */
    struct uref *flowdef = uref_dup(pic_flow);
    udict_dump(flowdef->udict, log);
    assert(flowdef);
    upipe_input(sws, flowdef, NULL);

    /* Define outputflow */
    uref_pic_set_hsize(pic_flow, DSTSIZE);
    uref_pic_set_vsize(pic_flow, DSTSIZE);
    assert(upipe_set_flow_def(sws, pic_flow));

    /* Now send pic */
    struct uref *pic = uref_dup(uref1);
    upipe_input(sws, pic, NULL);

    assert(sws_test_from_upipe(sws_test)->pic);
    assert(compare_chroma(((struct uref*[]){uref2, sws_test_from_upipe(sws_test)->pic}), "y8", 1, 1, 1, log));
    assert(compare_chroma(((struct uref*[]){uref2, sws_test_from_upipe(sws_test)->pic}), "u8", 2, 2, 1, log));
    assert(compare_chroma(((struct uref*[]){uref2, sws_test_from_upipe(sws_test)->pic}), "v8", 2, 2, 1, log));

    /* release urefs */
    uref_free(uref1);
    uref_free(uref2);
    uref_free(pic_flow);

    /* release pipes */
    upipe_release(sws);
    sws_test_free(sws_test);

    /* release managers */
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr); 
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}

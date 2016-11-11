/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe-swscale/upipe_sws.h>

#include <upipe/upipe_helper_upipe.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h> // debug

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define SRCSIZE             32
#define DSTSIZE             16

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
    }
    return UBASE_ERR_NONE;
}

enum plane_action {
    UNMAP,
    READ,
    WRITE
};

/** fetches a single chroma into slices and set corresponding stride */
static inline void fetch_chroma(struct uref *uref, const char *str, int *strides, uint8_t **slices, size_t idx, enum plane_action action)
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
    slices[3] = NULL;
    strides[3] = 0;
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

/** helper phony pipe */
struct sws_test {
    struct uref *pic;
    struct upipe upipe;
};

/** helper phony pipe */
UPIPE_HELPER_UPIPE(sws_test, upipe, 0);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct sws_test *sws_test = malloc(sizeof(struct sws_test));
    assert(sws_test != NULL);
    sws_test->pic = NULL;
	upipe_init(&sws_test->upipe, mgr, uprobe);
    upipe_throw_ready(&sws_test->upipe);
    return &sws_test->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct sws_test *sws_test = sws_test_from_upipe(upipe);
    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");

    if (sws_test->pic) {
        uref_free(sws_test->pic);
        sws_test->pic = NULL;
    }
    sws_test->pic = uref;
    upipe_dbg(upipe, "received pic");
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_dbg(upipe, "releasing pipe");
    upipe_throw_dead(upipe);
    struct sws_test *sws_test = sws_test_from_upipe(upipe);
    if (sws_test->pic) uref_free(sws_test->pic);
    upipe_clean(upipe);
    free(sws_test);
}

/** helper phony pipe */
static struct upipe_mgr sws_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

// DEBUG - from swscale/swscale_unscaled.c
static int check_image_pointers(const uint8_t * const data[4], enum AVPixelFormat pix_fmt, const int linesizes[4])
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
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

    struct SwsContext *img_convert_ctx;

    /* planar I420 */
    ubuf_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(ubuf_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "v8", 2, 2, 1));

    pic_flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(pic_flow != NULL);
    ubase_assert(uref_pic_flow_add_plane(pic_flow, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(pic_flow, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(pic_flow, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_align(pic_flow, UBUF_ALIGN));
//    uref_dump(pic_flow, mainlog);

    /* allocate reference picture */
    uref1 = uref_pic_alloc(uref_mgr, ubuf_mgr, SRCSIZE, SRCSIZE);
    assert(uref1 != NULL);
    assert(uref1->ubuf != NULL);
    ubase_assert(uref_pic_set_progressive(uref1));

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
    ubase_assert(uref_pic_set_progressive(uref2));

    img_convert_ctx = sws_getCachedContext(NULL,
                SRCSIZE, SRCSIZE, AV_PIX_FMT_YUV420P,
                DSTSIZE, DSTSIZE, AV_PIX_FMT_YUV420P,
                SWS_FULL_CHR_H_INP | SWS_ACCURATE_RND | SWS_LANCZOS,
                NULL, NULL, NULL);
    assert(img_convert_ctx);

    filldata(uref1, strides, slices, READ);
    filldata(uref2, dstrides, dslices, WRITE);

    assert(slices[0]);
    assert(slices[1]);
    assert(slices[2]);
    assert(strides[0]);
    assert(strides[1]);
    assert(strides[2]);

    assert(check_image_pointers((const uint8_t * const*) slices, AV_PIX_FMT_YUV420P, strides));
    assert(check_image_pointers((const uint8_t * const*) dslices, AV_PIX_FMT_YUV420P, dstrides));

    // fire raw swscale test
    sws_scale(img_convert_ctx, (const uint8_t * const*) slices, strides,
                               0, SRCSIZE,
                               dslices, dstrides);
    sws_freeContext(img_convert_ctx);

    filldata(uref1, strides, slices, UNMAP);
    filldata(uref2, dstrides, dslices, UNMAP);

    /*
     * now test upipe_sws module
     */

    /* build sws pipe and dependencies */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);
    struct upipe_mgr *upipe_sws_mgr = upipe_sws_mgr_alloc();
    assert(upipe_sws_mgr != NULL);

    /* Define outputflow */
    struct uref *output_flow = uref_dup(pic_flow);
    assert(output_flow != NULL);
    ubase_assert(uref_pic_flow_set_hsize(output_flow, DSTSIZE));
    ubase_assert(uref_pic_flow_set_vsize(output_flow, DSTSIZE));

    struct upipe *sws = upipe_flow_alloc(upipe_sws_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "sws"),
            output_flow); 
    assert(sws != NULL);
    ubase_assert(upipe_set_flow_def(sws, pic_flow));
    uref_free(output_flow);
    uref_free(pic_flow);

    /* build phony pipe */
    struct upipe *sws_test = upipe_void_alloc(&sws_test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "sws_test"));
    uprobe_dbg_va(logger, NULL, "Pipe addr: sws:\t %p", sws);
    uprobe_dbg_va(logger, NULL, "Pipe addr: sws_test: %p", sws_test);
    assert(sws_test);

    /* connect upipe_sws output to sws_test */
    ubase_assert(upipe_set_output(sws, sws_test));

    /* Now send pic */
    struct uref *pic = uref_dup(uref1);
    upipe_input(sws, pic, NULL);

    assert(sws_test_from_upipe(sws_test)->pic);
    assert(compare_chroma(((struct uref*[]){uref2, sws_test_from_upipe(sws_test)->pic}), "y8", 1, 1, 1, logger));
    assert(compare_chroma(((struct uref*[]){uref2, sws_test_from_upipe(sws_test)->pic}), "u8", 2, 2, 1, logger));
    assert(compare_chroma(((struct uref*[]){uref2, sws_test_from_upipe(sws_test)->pic}), "v8", 2, 2, 1, logger));

    /* release urefs */
    uref_free(uref1);
    uref_free(uref2);

    /* release pipes */
    upipe_release(sws);
    test_free(sws_test);

    /* release managers */
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr); 
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}

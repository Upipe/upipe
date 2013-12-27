/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short unit tests for upipe x264 module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_block.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>

#include <upipe-x264/upipe_x264.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET  0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define WIDTH               96
#define HEIGHT              64
#define LIMIT               60


/** phony pipe to test upipe_x264 */
struct x264_test {
    int counter;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_x264 */
UPIPE_HELPER_UPIPE(x264_test, upipe, 0);

/** helper phony pipe to test upipe_x264 */
static struct upipe *x264_test_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct x264_test *x264_test = malloc(sizeof(struct x264_test));
    assert(x264_test != NULL);
    upipe_init(&x264_test->upipe, mgr, uprobe);
    x264_test->counter = 0;
    upipe_throw_ready(&x264_test->upipe);
    return &x264_test->upipe;
}

/** helper phony pipe to test upipe_x264 */
static void x264_test_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct x264_test *x264_test = x264_test_from_upipe(upipe);
    const char *def;
    uint64_t pts = 0, dts = 0;
    if (uref->udict != NULL) {
        udict_dump(uref->udict, upipe->uprobe);
    }
    if (unlikely(uref_flow_get_def(uref, &def))) {
        upipe_notice_va(upipe, "flow definition for %s", def);
        goto end;
    }
    if (unlikely(!uref->ubuf)) {
        upipe_dbg(upipe, "dropping empty uref ref");
        goto end;
    }

    if (!uref_clock_get_pts_prog(uref, &pts)) {
        upipe_warn(upipe, "received packet with no pts");
    }
    if (!uref_clock_get_dts_prog(uref, &dts)) {
        upipe_warn(upipe, "received packet with no dts");
    }
    upipe_dbg_va(upipe, "received pic %d, pts: %"PRIu64" , dts: %"PRIu64,
                 x264_test->counter, pts, dts);
    x264_test->counter++;

end:
    uref_free(uref);
}

/** helper phony pipe to test upipe_x264 */
static void x264_test_free(struct upipe *upipe)
{
    struct x264_test *x264_test = x264_test_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(x264_test);
}

/** helper phony pipe to test upipe_x264 */
static struct upipe_mgr x264_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = x264_test_alloc,
    .upipe_input = x264_test_input,
    .upipe_control = NULL,
};


static void fill_pic(struct uref *uref, int counter)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    assert(uref_pic_size(uref, &hsize, &vsize, &macropixel));

    const char *chroma = NULL;
    while (uref_pic_plane_iterate(uref, &chroma) && chroma != NULL) {
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        assert(uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub,
                                   &macropixel_size));
        int hoctets = hsize * macropixel_size / hsub / macropixel;
        uint8_t *buffer;
        assert(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buffer));

        for (int y = 0; y < vsize / vsub; y++) {
            for (int x = 0; x < hoctets; x++)
                buffer[x] = 1 + (y * hoctets) + x + counter * 5;
            buffer += stride;
        }
        assert(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1));
    }
}

/** definition of our uprobe */
static enum ubase_err catch(struct uprobe *uprobe, struct upipe *upipe,
                            enum uprobe_event event, va_list args)
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

int main(int argc, char **argv)
{
    printf("Compiled %s %s (%s)\n", __DATE__, __TIME__, __FILE__);

    struct uref *pic;
    int counter;
    uint64_t pts;

    /* upipe env */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    /* planar YUV (I420) */
    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    assert(pic_mgr != NULL);
    assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8", 1, 1, 1));
    assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "u8", 2, 2, 1));
    assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "v8", 2, 2, 1));
    /* block */
    struct ubuf_mgr *block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr,
            UBUF_ALIGN,
            UBUF_ALIGN_OFFSET);
    assert(block_mgr);

    /* x264 manager */
    struct upipe_mgr *upipe_x264_mgr = upipe_x264_mgr_alloc();

    /* send flow definition */
    struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def);
    assert(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"));
    assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"));
    assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"));
    assert(uref_pic_flow_set_hsize(flow_def, WIDTH));
    assert(uref_pic_flow_set_vsize(flow_def, HEIGHT));
    struct urational fps = { .num = 25, .den = 1 };
    assert(uref_pic_flow_set_fps(flow_def, fps));

    /* x264 pipe */
    struct upipe *x264 = upipe_void_alloc(upipe_x264_mgr,
                    uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                     "x264"));
    assert(x264);
    assert(upipe_set_flow_def(x264, flow_def));
    uref_free(flow_def);
    assert(upipe_set_ubuf_mgr(x264, block_mgr));

    /* x264_test */
    struct upipe *x264_test = upipe_void_alloc(&x264_test_mgr,
                    uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                     "x264_test"));
    upipe_set_output(x264, x264_test);

    /* test controls */
    assert(upipe_x264_set_default_preset(x264, "placebo", "film"));
    assert(upipe_x264_set_profile(x264, "baseline"));
    assert(upipe_x264_set_default_preset(x264, "faster", NULL));
    assert(upipe_x264_set_profile(x264, "high"));
    assert(upipe_x264_set_default(x264));
    
    /* encoding test */
    for (counter = 0; counter < LIMIT; counter ++) {
        printf("Sending pic %d\n", counter);
        pic = uref_pic_alloc(uref_mgr, pic_mgr, WIDTH, HEIGHT);
        assert(pic);
        fill_pic(pic, counter);
        pts = counter + 42;
        uref_clock_set_pts_orig(pic, pts);
        uref_clock_set_pts_prog(pic, pts * UCLOCK_FREQ + UINT32_MAX);
        upipe_input(x264, pic, NULL);
    }

    /* release pipes */
    upipe_release(x264);
    x264_test_free(x264_test);

    /* clean everything */
    upipe_mgr_release(upipe_x264_mgr); // noop
    ubuf_mgr_release(pic_mgr);
    ubuf_mgr_release(block_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}

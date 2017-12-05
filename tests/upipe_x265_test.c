/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Clément Vasseur
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
 * @short unit tests for upipe x265 module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_ubuf_mem.h>
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

#include <upipe-x265/upipe_x265.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define UPROBE_LOG_LEVEL    UPROBE_LOG_DEBUG
#define WIDTH               96
#define HEIGHT              64
#define LIMIT               8


/** phony pipe to test upipe_x265 */
struct x265_test {
    int counter;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_x265 */
UPIPE_HELPER_UPIPE(x265_test, upipe, 0);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct x265_test *x265_test = malloc(sizeof(struct x265_test));
    assert(x265_test != NULL);
    upipe_init(&x265_test->upipe, mgr, uprobe);
    x265_test->counter = 0;
    upipe_throw_ready(&x265_test->upipe);
    return &x265_test->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct x265_test *x265_test = x265_test_from_upipe(upipe);
    uint64_t pts = 0, dts = 0;
    if (uref->udict != NULL) {
        udict_dump(uref->udict, upipe->uprobe);
    }
    if (!ubase_check(uref_clock_get_pts_prog(uref, &pts))) {
        upipe_warn(upipe, "received packet with no pts");
    }
    if (!ubase_check(uref_clock_get_dts_prog(uref, &dts))) {
        upipe_warn(upipe, "received packet with no dts");
    }
    upipe_dbg_va(upipe, "received pic %d, pts: %"PRIu64" , dts: %"PRIu64,
                 x265_test->counter, pts, dts);
    x265_test->counter++;

    uref_free(uref);
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
    struct x265_test *x265_test = x265_test_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(x265_test);
}

/** helper phony pipe */
static struct upipe_mgr x265_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};


static void fill_pic(struct uref *uref, int counter)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    assert(ubase_check(uref_pic_size(uref, &hsize, &vsize, &macropixel)));

    const char *chroma = NULL;
    while (ubase_check(uref_pic_plane_iterate(uref, &chroma)) && chroma != NULL) {
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        assert(ubase_check(uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub,
                                   &macropixel_size)));
        int hoctets = hsize * macropixel_size / hsub / macropixel;
        uint8_t *buffer;
        assert(ubase_check(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buffer)));

        for (int y = 0; y < vsize / vsub; y++) {
            for (int x = 0; x < hoctets; x++)
                buffer[x] = 1 + (y * hoctets) + x + counter * 5;
            buffer += stride;
        }
        assert(ubase_check(uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1)));
    }
}

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

int main(int argc, char **argv)
{
    printf("Compiled %s %s (%s)\n", __DATE__, __TIME__, __FILE__);

    struct uref *pic;
    int counter;
    uint64_t pts;

    /* upipe env */
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
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "v8", 2, 2, 1));

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* x265 manager */
    struct upipe_mgr *upipe_x265_mgr = upipe_x265_mgr_alloc();

    /* send flow definition */
    struct uref *flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def);
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow_def, WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, HEIGHT));
    struct urational fps = { .num = 25, .den = 1 };
    ubase_assert(uref_pic_flow_set_fps(flow_def, fps));
    ubase_assert(uref_pic_set_progressive(flow_def));

    /* x265 pipe */
    struct upipe *x265 = upipe_void_alloc(upipe_x265_mgr,
                    uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                     "x265"));
    assert(x265);
    ubase_assert(upipe_set_flow_def(x265, flow_def));
    uref_free(flow_def);

    /* x265_test */
    struct upipe *x265_test = upipe_void_alloc(&x265_test_mgr,
                    uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                     "x265_test"));
    ubase_assert(upipe_set_output(x265, x265_test));

    /* test controls */
    ubase_assert(upipe_x265_set_default_preset(x265, "placebo", "grain"));
    ubase_assert(upipe_x265_set_profile(x265, "main"));
    ubase_assert(upipe_x265_set_default_preset(x265, "faster", NULL));
    ubase_assert(upipe_x265_set_profile(x265, "mainstillpicture"));
    ubase_assert(upipe_x265_set_default(x265, 0));
    ubase_assert(upipe_x265_set_default_preset(x265, "ultrafast", NULL));

    /* disable assembly (not valgrind safe) */
    ubase_assert(upipe_set_option(x265, "asm", "0"));

    /* encoding test */
    for (counter = 0; counter < LIMIT; counter ++) {
        printf("Sending pic %d\n", counter);
        pic = uref_pic_alloc(uref_mgr, pic_mgr, WIDTH, HEIGHT);
        assert(pic);
        fill_pic(pic, counter);
        pts = counter + 42;
        uref_clock_set_pts_orig(pic, pts);
        uref_clock_set_pts_prog(pic, pts * UCLOCK_FREQ + UINT32_MAX);
        upipe_input(x265, pic, NULL);
    }

    /* release pipes */
    upipe_release(x265);
    test_free(x265_test);

    /* clean everything */
    upipe_mgr_release(upipe_x265_mgr);
    upipe_x265_cleanup();
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    return 0;
}

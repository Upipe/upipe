/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
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
#include <upipe/upipe.h>
#include <upipe-filters/upipe_filter_blend.h>
#include <upipe-modules/upipe_null.h>

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
#define UBUF_ALIGN          32
#define UBUF_ALIGN_HOFFSET  0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define WIDTH               720
#define HEIGHT              576

static struct ubuf_mgr *ubuf_mgr;
static struct uref_mgr *uref_mgr;

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
    static int counter = 0; 
    int x, y;
    uint8_t *buf, macropixel = 0;
    size_t stride = 0;
    struct uref *pic;

    /* upipe env */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    /* rgb24 */
    ubuf_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_PREPEND, UBUF_APPEND,
                                      UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(ubuf_mgr);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(ubuf_mgr, "r8g8b8", 1, 1, 3));

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* nullpipe */
    struct upipe_mgr *null_mgr = upipe_null_mgr_alloc();
    struct upipe *nullpipe = upipe_void_alloc(null_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "null"));
    assert(nullpipe);
    ubase_assert(upipe_null_dump_dict(nullpipe, true));

    struct uref *uref = uref_pic_flow_alloc_def(uref_mgr, 3);
    ubase_assert(uref_pic_flow_add_plane(uref, 1, 1, 3, "r8g8b8"));
    assert(uref);

    /* blend */
    struct upipe_mgr *blend_mgr = upipe_filter_blend_mgr_alloc();
    struct upipe *filter_blend = upipe_void_alloc(blend_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "blend"));
    assert(filter_blend);
    ubase_assert(upipe_set_flow_def(filter_blend, uref));
    ubase_assert(upipe_set_output(filter_blend, nullpipe));
    upipe_release(nullpipe);
    uref_free(uref);

    for (counter=0; counter < 10; counter++) {
        printf("Sending pic %d\n", counter);
        pic = uref_pic_alloc(uref_mgr, ubuf_mgr, WIDTH, HEIGHT);
        assert(pic);
        ubase_assert(uref_pic_plane_write(pic, "r8g8b8", 0, 0, -1, -1, &buf));
        ubase_assert(uref_pic_plane_size(pic, "r8g8b8", &stride, NULL, NULL, &macropixel));
        for (y=0; y < HEIGHT; y++) {
            for (x=0; x < WIDTH; x++) {
                buf[macropixel * x] = x + y + counter * 3;
                buf[macropixel * x + 1] = x + y + counter * 3 * 10;
                buf[macropixel * x + 2] = x + y + counter * 3 * 10;
            }
            buf += stride;
        }
        uref_pic_plane_unmap(pic, "r8g8b8", 0, 0, -1, -1);
        upipe_input(filter_blend, pic, NULL);
    }

    // Clean - release
    upipe_release(filter_blend);

    upipe_mgr_release(blend_mgr); // noop
    upipe_mgr_release(null_mgr); // noop
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

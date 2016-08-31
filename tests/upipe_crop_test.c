/*
 * Copyright (C) 2016 OpenHeadend S.A.R.L.
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


#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe-modules/upipe_crop.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define BGSIZE              32
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

static unsigned int step;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

/* fill picture with some reference */
static void fill_in(struct uref *uref, const char *chroma, uint8_t val)
{
    uint8_t hsub, vsub, macropixel_size;
    size_t hsize, vsize, stride;
    uint8_t *buffer;
    ubase_assert(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buffer));
    ubase_assert(uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub, &macropixel_size));
    assert(buffer != NULL);
    ubase_assert(uref_pic_size(uref, &hsize, &vsize, NULL));
    hsize /= hsub;
    hsize *= macropixel_size;
    vsize /= vsub;
    for (int y = 0; y < vsize; y++) {
        for (int x = 0; x < hsize; x++)
            buffer[x] = val++;
        buffer += stride;
    }
    uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
}


/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
	upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);

    size_t hsize, vsize;
    uint8_t macropixel;
    ubase_assert(uref_pic_size(uref, &hsize, &vsize, &macropixel));
    assert(macropixel == 1);

    const uint8_t *r;
    ubase_assert(uref_pic_plane_read(uref, "y8", 0, 0, -1, -1, &r));

    switch (step) {
        case 0:
        case 1:
            assert(hsize == 28);
            assert(vsize == 28);
            assert(r[0] == 2 + BGSIZE * 2);
            break;
        case 2:
            assert(hsize == 32);
            assert(vsize == 32);
            assert(r[0] == 0);
            break;
        case 3:
            assert(hsize == 30);
            assert(vsize == 30);
            assert(r[0] == 0);
            break;
        default:
            assert(0);
    }

    ubase_assert(uref_pic_plane_unmap(uref, "y8", 0, 0, -1, -1));
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    if (command != UPIPE_SET_FLOW_DEF)
        assert(0);

    struct uref *flow_def = va_arg(args, struct uref *);
    ubase_assert(uref_flow_match_def(flow_def, "pic."));
    ubase_assert(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_check_chroma(flow_def, 2, 2, 1, "v8"));

    uint64_t hsize, vsize, lpad = 0, rpad = 0, tpad = 0, bpad = 0;
    ubase_assert(uref_pic_flow_get_hsize(flow_def, &hsize));
    ubase_assert(uref_pic_flow_get_vsize(flow_def, &vsize));
    uref_pic_get_lpadding(flow_def, &lpad);
    uref_pic_get_rpadding(flow_def, &rpad);
    uref_pic_get_tpadding(flow_def, &tpad);
    uref_pic_get_bpadding(flow_def, &bpad);

    switch (step) {
        case 0:
        case 1:
            assert(hsize == 28);
            assert(vsize == 28);
            assert(lpad == 0);
            assert(rpad == 0);
            assert(tpad == 0);
            assert(bpad == 0);
            break;
        case 2:
            assert(hsize == 32);
            assert(vsize == 32);
            assert(lpad == 2);
            assert(rpad == 2);
            assert(tpad == 2);
            assert(bpad == 2);
            break;
        case 3:
            assert(hsize == 30);
            assert(vsize == 30);
            assert(lpad == 2);
            assert(rpad == 0);
            assert(tpad == 2);
            assert(bpad == 0);
            break;
        default:
            assert(0);
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_dbg(upipe, "releasing pipe");
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc_fourcc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, "I420", 0, 0, 0, 0, 0, 0);
    assert(pic_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    /* build crop pipe */
    struct upipe_mgr *upipe_crop_mgr = upipe_crop_mgr_alloc();
    assert(upipe_crop_mgr);

    struct upipe *crop = upipe_void_alloc(upipe_crop_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "crop"));
    assert(crop != NULL);

    /* build phony pipe */
    struct upipe *test = upipe_void_alloc(&test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "test"));
    assert(test != NULL);
    ubase_assert(upipe_set_output(crop, test));

    struct uref *flow_def;
    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow_def, 32));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, 32));
    ubase_assert(upipe_set_flow_def(crop, flow_def));
    uref_free(flow_def);

    struct uref *uref;
    uref = uref_pic_alloc(uref_mgr, pic_mgr, BGSIZE, BGSIZE);
    assert(uref != NULL);
    fill_in(uref, "y8", 0);
    fill_in(uref, "u8", 0);
    fill_in(uref, "v8", 0);

    step = 0;
    upipe_crop_set_rect(crop, 2, 2, 2, 2);
    upipe_input(crop, uref_dup(uref), NULL);

    step = 1;
    upipe_crop_set_rect(crop, 3, 3, 3, 3);
    upipe_input(crop, uref_dup(uref), NULL);

    step = 2;
    upipe_crop_set_rect(crop, -2, -2, -2, -2);
    upipe_input(crop, uref_dup(uref), NULL);

    step = 3;
    upipe_crop_set_rect(crop, -2, 2, -2, 2);
    upipe_input(crop, uref_dup(uref), NULL);

    ubase_nassert(upipe_crop_set_rect(crop, 18, 18, 18, 18));

    /* release crop pipe */
    uref_free(uref);
    upipe_release(crop);
    test_free(test);

    /* release managers */
    upipe_mgr_release(upipe_crop_mgr); // no-op
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

/*
 * Copyright (C) 2020 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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
#include <upipe/uprobe_ubuf_mem.h>
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
#include <upipe-modules/upipe_pad.h>

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
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

#define PAD_L 2
#define PAD_R 4
#define PAD_T 5
#define PAD_B 1

#define INPUT_W 32
#define INPUT_H 16

#define TOTAL_W INPUT_W + PAD_L + PAD_R
#define TOTAL_H INPUT_H + PAD_T + PAD_B

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
            buffer[x] = val;
        buffer += stride;
    }
    uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
}

/* check a chroma */
static void check_chroma(struct uref *uref, const char *chroma, uint8_t val)
{
    uint8_t hsub, vsub, macropixel_size;
    size_t hsize, vsize, stride;
    const uint8_t *buffer;
    int x, y;

    ubase_assert(uref_pic_plane_read(uref, chroma, 0, 0, -1, -1, &buffer));
    ubase_assert(uref_pic_plane_size(uref, chroma, &stride, &hsub, &vsub, &macropixel_size));
    ubase_assert(uref_pic_size(uref, &hsize, &vsize, NULL));
    hsize /= hsub;
    hsize *= macropixel_size;
    vsize /= vsub;

    for (y = 0; y < vsize; y++) {
        for (x = 0; x < hsize; x++) {
            assert(buffer[x] == val);
        }
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

    /* Check left. */
    ubase_assert(uref_pic_resize(uref, 0, 0, PAD_L, TOTAL_H));
    check_chroma(uref, "y8", 0);
    check_chroma(uref, "u8", 0x80);
    check_chroma(uref, "v8", 0x80);
    ubase_assert(uref_pic_resize(uref, 0, 0, TOTAL_W, TOTAL_H));

    /* Check top. */
    ubase_assert(uref_pic_resize(uref, 0, 0, TOTAL_W, PAD_T));
    check_chroma(uref, "y8", 0);
    check_chroma(uref, "u8", 0x80);
    check_chroma(uref, "v8", 0x80);
    ubase_assert(uref_pic_resize(uref, 0, 0, TOTAL_W, TOTAL_H));

    /* Check middle. */
    ubase_assert(uref_pic_resize(uref, PAD_L, PAD_T, INPUT_W, INPUT_H));
    check_chroma(uref, "y8", 1);
    check_chroma(uref, "u8", 128);
    check_chroma(uref, "v8", 255);
    ubase_assert(uref_pic_resize(uref, -PAD_L, -PAD_T, TOTAL_W, TOTAL_H));

    /* Check right. */
    ubase_assert(uref_pic_resize(uref, INPUT_W + PAD_L, 0, PAD_R, TOTAL_H));
    check_chroma(uref, "y8", 0);
    check_chroma(uref, "u8", 0x80);
    check_chroma(uref, "v8", 0x80);
    ubase_assert(uref_pic_resize(uref, -(INPUT_W + PAD_L), 0, TOTAL_W, TOTAL_H));

    /* Check bottom. */
    ubase_assert(uref_pic_resize(uref, 0, INPUT_H + PAD_T, TOTAL_W, PAD_B));
    check_chroma(uref, "y8", 0);
    check_chroma(uref, "u8", 0x80);
    check_chroma(uref, "v8", 0x80);
    ubase_assert(uref_pic_resize(uref, 0, -(INPUT_H + PAD_T), TOTAL_W, TOTAL_H));

    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            ubase_assert(uref_flow_match_def(flow_def, "pic."));
            ubase_assert(uref_pic_flow_match_hsize(flow_def, TOTAL_W, TOTAL_W));
            ubase_assert(uref_pic_flow_match_vsize(flow_def, TOTAL_H, TOTAL_H));
            ubase_assert(uref_pic_flow_check_chroma(flow_def, 1, 1, 1, "y8"));
            ubase_assert(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "u8"));
            ubase_assert(uref_pic_flow_check_chroma(flow_def, 2, 1, 1, "v8"));
            return UBASE_ERR_NONE;
        }
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
            UBUF_POOL_DEPTH, umem_mgr, "YV16", 0, 0, 0, 0, 0, 0);
    assert(pic_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, 0, 0);
    assert(logger != NULL);

    /* build pad pipe */
    struct upipe_mgr *upipe_pad_mgr = upipe_pad_mgr_alloc();
    assert(upipe_pad_mgr);

    struct uref *flow_def = uref_alloc(uref_mgr);
    assert(flow_def);
    ubase_assert(uref_flow_set_def(flow_def, "pic."));
    ubase_assert(uref_pic_set_lpadding(flow_def, PAD_L));
    ubase_assert(uref_pic_set_rpadding(flow_def, PAD_R));
    ubase_assert(uref_pic_set_tpadding(flow_def, PAD_T));
    ubase_assert(uref_pic_set_bpadding(flow_def, PAD_B));

    struct upipe *pad = upipe_flow_alloc(upipe_pad_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "pad"),
            flow_def);
    assert(pad != NULL);
    uref_free(flow_def);

    /* build phony pipe */
    struct upipe *test = upipe_void_alloc(&test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "test"));
    assert(test != NULL);
    ubase_assert(upipe_set_output(pad, test));

    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 1, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 1, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow_def, INPUT_W));
    ubase_assert(uref_pic_flow_set_vsize(flow_def, INPUT_H));
    ubase_assert(upipe_set_flow_def(pad, flow_def));
    uref_free(flow_def);

    struct uref *uref;
    uref = uref_pic_alloc(uref_mgr, pic_mgr, INPUT_W, INPUT_H);
    assert(uref != NULL);
    uref_pic_set_progressive(uref);
    fill_in(uref, "y8", 1);
    fill_in(uref, "u8", 128);
    fill_in(uref, "v8", 255);
    upipe_input(pad, uref, NULL);

    /* release pad pipe and subpipes */
    upipe_release(pad);
    test_free(test);

    /* release managers */
    upipe_mgr_release(upipe_pad_mgr); // no-op
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

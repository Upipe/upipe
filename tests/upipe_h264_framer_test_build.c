/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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
 *
 */

/** @file
 * @short utility to prepare the structures for the H264 framer unit test
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe/uclock.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upipe-framers/uref_h26x_flow.h>
#include <upipe-x264/upipe_x264.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <bitstream/mpeg/h264.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE
#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UBUF_SHARED_POOL_DEPTH 0
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET  0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define WIDTH               96
#define HEIGHT              64

static void dump_variable(const char *variable,
                          const uint8_t *buffer, size_t size)
{
    printf("static const uint8_t %s[%zu] = {\n", variable, size);

    for (int i = 0; i < size; i++) {
        printf("0x%02"PRIx8, buffer[i]);
        if (i != size - 1)
            printf(", ");
        if (!((i + 1) % 12))
            printf("\n");
    }

    printf("};\n");
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
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    size_t size;
    ubase_assert(uref_block_size(uref, &size));
    uint8_t buffer[size + 5];
    ubase_assert(uref_block_extract(uref, 0, size, buffer));
    buffer[size++] = 0;
    buffer[size++] = 0;
    buffer[size++] = 0;
    buffer[size++] = 1;
    buffer[size++] = H264NAL_TYPE_ENDSTR;
    dump_variable("h264_pic", buffer, size);
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            const uint8_t *headers;
            size_t size;
            if (ubase_check(uref_flow_get_headers(flow_def, &headers, &size)))
                dump_variable("h264_headers", headers, size);
            return UBASE_ERR_NONE;
        }
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            if (urequest->type == UREQUEST_FLOW_FORMAT) {
                struct uref *uref = uref_dup(urequest->uref);
                assert(uref != NULL);
                ubase_assert(uref_flow_set_global(uref));
                return urequest_provide_flow_format(urequest, uref);
            }
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
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

static void fill_pic(struct uref *uref, int counter)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    assert(ubase_check(uref_pic_size(uref, &hsize, &vsize, &macropixel)));

    uref_pic_plane_foreach(uref, chroma) {
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

int main(int argc, char **argv)
{
    /* structures managers */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);

    /* probes */
    struct uprobe uprobe_s;
    uprobe_init(&uprobe_s, catch, NULL);
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_alloc(&uprobe_s, stderr, UPROBE_LOG_LEVEL);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(uprobe != NULL);

    struct upipe *sink = upipe_void_alloc(&test_mgr, uprobe_use(uprobe));
    assert(sink != NULL);

    /* planar YUV (I420) */
    struct ubuf_mgr *pic_mgr =
        ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                               UBUF_PREPEND, UBUF_APPEND,
                               UBUF_PREPEND, UBUF_APPEND,
                               UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    assert(pic_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "v8", 2, 2, 1));
    struct uref *uref = uref_pic_alloc(uref_mgr, pic_mgr, WIDTH, HEIGHT);
    fill_pic(uref, 0);

    /* send flow definition */
    struct uref *x264_flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(x264_flow_def);
    ubase_assert(uref_pic_flow_add_plane(x264_flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(x264_flow_def, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(x264_flow_def, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(x264_flow_def, WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(x264_flow_def, HEIGHT));
    struct urational fps = { .num = 25, .den = 1 };
    ubase_assert(uref_pic_flow_set_fps(x264_flow_def, fps));

    /* x264 pipe */
    struct upipe_mgr *x264_mgr = upipe_x264_mgr_alloc();
    assert(x264_mgr != NULL);
    struct upipe *x264 = upipe_void_alloc(x264_mgr,
                    uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_LEVEL,
                                     "x264"));
    assert(x264 != NULL);
    ubase_assert(upipe_x264_set_default_preset(x264, "faster", "zerolatency"));
    ubase_assert(upipe_x264_set_profile(x264, "high"));
    ubase_assert(upipe_set_option(x264, "bitrate", "100"));
    ubase_assert(upipe_set_option(x264, "vbv-bufsize", "100"));
    ubase_assert(upipe_set_option(x264, "aud", "0"));
    ubase_assert(upipe_set_option(x264, "repeat-headers", "0"));
    ubase_assert(upipe_set_option(x264, "nal-hrd", "vbr"));
    ubase_assert(upipe_set_option(x264, "keyint", "1"));
    ubase_assert(upipe_set_flow_def(x264, x264_flow_def));
    ubase_assert(upipe_set_output(x264, sink));

    uref = uref_pic_alloc(uref_mgr, pic_mgr, WIDTH, HEIGHT);
    fill_pic(uref, 0);
    upipe_input(x264, uref_dup(uref), NULL);
    upipe_release(x264);

    uref_free(uref);
    test_free(sink);
    uref_free(x264_flow_def);

    upipe_mgr_release(x264_mgr);
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(pic_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe);
    uprobe_clean(&uprobe_s);

    return 0;
}

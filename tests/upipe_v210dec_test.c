/*
 * Copyright (C) 2017 Open Broadcast Systems
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

/** @file
 * @short unit tests for v210 decoder
 */

#undef NDEBUG

#include <upipe/ubuf_pic_mem.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/upipe.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_std.h>

#include <upipe-v210/upipe_v210dec.h>

#include <libavutil/common.h>
#include <libavutil/intreadwrite.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

#define UBUF_ALIGN 32

#define TEST_WIDTH 1920
#define TEST_HEIGHT 1

const char *v210_chroma = "u10y10v10y10u10y10v10y10u10y10v10y10";

#define CLIP_10(v) av_clip(v, 4, 1019)

#define WRITE_PIXELS_10(a, b, c)           \
    do {                                \
        val  =  CLIP_10(a);             \
        val |= (CLIP_10(b) << 10);     \
        val |= (CLIP_10(c) << 20);      \
        AV_WL32(dst, val);              \
        dst += 4;                       \
    } while (0)

/* fill picture with some reference */
static void fill_in(struct uref *uref)
{
    size_t hsize, vsize, stride;
    uint8_t *buffer = 0;
    ubase_assert(uref_pic_plane_write(uref, v210_chroma, 0, 0, -1, -1, &buffer));
    ubase_assert(uref_pic_plane_size(uref, v210_chroma, &stride, NULL, NULL, NULL));
    assert(buffer);
    ubase_assert(uref_pic_size(uref, &hsize, &vsize, NULL));
    for (int y = 0; y < vsize; y++) {
        uint8_t *dst = buffer;
        for (int x = 0; x < hsize - 5; x += 6) {
            uint32_t val;
            WRITE_PIXELS_10(256, 512, 768);
            WRITE_PIXELS_10(256, 512, 768);
            WRITE_PIXELS_10(256, 512, 768);
            WRITE_PIXELS_10(256, 512, 768);
        }
        buffer += stride;
    }
    uref_pic_plane_unmap(uref, v210_chroma, 0, 0, -1, -1);
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
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe);
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

bool test_sucessful = false;

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref);
    upipe_dbg(upipe, "===> received input uref");

    const uint8_t *buffer;
    size_t w, h, stride;

    ubase_assert(uref_pic_size(uref, &w, &h, NULL));
    assert(w > 0);
    assert(h > 0);

    if (ubase_check(uref_pic_plane_read(uref, "y8", 0, 0, -1, -1, &buffer)) &&
        ubase_check(uref_pic_plane_size(uref, "y8", &stride, NULL, NULL, NULL))) {
        /* test y8 plane */
        for (int y = 0; y < h; y++) {
            const uint8_t *src = buffer;
            for (int x = 0; x < w-2; x += 3) {
                assert(src[0] == 128);
                assert(src[1] == 64);
                assert(src[2] == 192);
            }
            buffer += stride;
        }

        upipe_dbg(upipe, "y8 plane tested correctly");
        uref_pic_plane_unmap(uref, "y8", 0, 0, -1, -1);
        test_sucessful = true;
    }

    else if (ubase_check(uref_pic_plane_read(uref, "y10l", 0, 0, -1, -1, &buffer)) &&
             ubase_check(uref_pic_plane_size(uref, "y10l", &stride, NULL, NULL, NULL))) {
        /* test y10 plane */
        for (int y = 0; y < h; y++) {
            const uint16_t *src = (uint16_t*)buffer;
            for (int x = 0; x < w-2; x += 3) {
                assert(src[0] == 512);
                assert(src[1] == 256);
                assert(src[2] == 768);
            }
            buffer += stride;
        }

        upipe_dbg(upipe, "y10 plane tested correctly");
        uref_pic_plane_unmap(uref, "y10l", 0, 0, -1, -1);
        test_sucessful = true;
    }

    else {
        upipe_err(upipe, "unknown chroma format");
    }

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
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    upipe_dbg_va(upipe, "caught event %d: %s", event, uprobe_event_str(event));
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
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr);

    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
            umem_mgr, -1, -1);
    assert(udict_mgr);

    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH,
            udict_mgr, 0);
    assert(uref_mgr);

    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, 1, -1, -1, -1, -1, UBUF_ALIGN, 0);
    assert(pic_mgr);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, v210_chroma, 1, 1, 1));

    /* allocate reference picture */
    struct uref *input_uref = uref_pic_alloc(uref_mgr, pic_mgr,
            TEST_WIDTH, TEST_HEIGHT);
    assert(input_uref);
    assert(input_uref->ubuf);
    /* fill reference picture */
    fill_in(input_uref);

    /* create input flow definition */
    struct uref *in_flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(in_flow_def);
    ubase_assert(uref_pic_flow_add_plane(in_flow_def, 1, 1, 16, v210_chroma));
    ubase_assert(uref_pic_flow_set_hsize(in_flow_def, TEST_WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(in_flow_def, TEST_HEIGHT));
    ubase_assert(uref_pic_flow_set_macropixel(in_flow_def, 6));
    ubase_assert(uref_pic_flow_set_align(in_flow_def, UBUF_ALIGN));

    /* create output flow definition */
    struct uref *out_flow_8 = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(out_flow_8);
    ubase_assert(uref_pic_flow_add_plane(out_flow_8, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(out_flow_8, 2, 1, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(out_flow_8, 2, 1, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(out_flow_8, TEST_WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(out_flow_8, TEST_HEIGHT));

    struct uref *out_flow_10 = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(out_flow_10);
    ubase_assert(uref_pic_flow_add_plane(out_flow_10, 1, 1, 2, "y10l"));
    ubase_assert(uref_pic_flow_add_plane(out_flow_10, 2, 1, 2, "u10l"));
    ubase_assert(uref_pic_flow_add_plane(out_flow_10, 2, 1, 2, "v10l"));
    ubase_assert(uref_pic_flow_set_hsize(out_flow_10, TEST_WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(out_flow_10, TEST_HEIGHT));

    /* create a probe */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);

    /* create a stdio message probe */
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
            UPROBE_LOG_LEVEL);
    assert(logger);

    /* create a memory manager probe */
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(logger);

    /* create message prefix probes */
    struct uprobe *logger_v210 = uprobe_pfx_alloc(uprobe_use(logger),
            UPROBE_LOG_LEVEL, "v210dec");
    struct uprobe *logger_test = uprobe_pfx_alloc(uprobe_use(logger),
            UPROBE_LOG_LEVEL, "test");
    assert(logger);

    /* build v210dec pipe */
    struct upipe_mgr *upipe_v210dec_mgr = upipe_v210dec_mgr_alloc();
    assert(upipe_v210dec_mgr);
    struct upipe *v210dec = upipe_flow_alloc(upipe_v210dec_mgr, uprobe_use(logger_v210), out_flow_10);
    assert(v210dec);

    /* build phony pipe */
    struct upipe *test = upipe_void_alloc(&test_mgr, uprobe_use(logger_test));
    assert(test);
    ubase_assert(upipe_set_output(v210dec, test));

    /* put the input flow definition into the pipe */
    ubase_assert(upipe_set_flow_def(v210dec, in_flow_def));

    /* send the picture */
    struct uref *pic = uref_dup(input_uref);
    assert(pic);
    upipe_input(v210dec, pic, 0);

    uref_free(in_flow_def);
    uref_free(out_flow_8);
    uref_free(out_flow_10);
    /* release v210dec pipe */
    uref_free(input_uref);
    upipe_release(v210dec);
    test_free(test);

    /* release managers */
    upipe_mgr_release(upipe_v210dec_mgr); // no-op
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger_test);
    uprobe_release(logger_v210);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return !test_sucessful;
}

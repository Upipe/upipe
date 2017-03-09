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
 * @short unit tests for v210 encoder
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

#include <upipe-v210/upipe_v210enc.h>

#include <libavutil/intreadwrite.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

#define TEST_WIDTH 1920
#define TEST_HEIGHT 1

#define VALUE_Y 64
#define VALUE_U 128
#define VALUE_V 192

const char *v210_chroma = "u10y10v10y10u10y10v10y10u10y10v10y10";

/* fill picture with some reference */
static void fill_in(struct uref *uref,
                    const char *chroma, uint8_t hsub, uint8_t vsub,
                    uint8_t macropixel_size, int value)
{
    size_t hsize, vsize, stride;
    uint8_t *buffer;
    ubase_assert(uref_pic_plane_write(uref, chroma, 0, 0, -1, -1, &buffer));
    ubase_assert(uref_pic_plane_size(uref, chroma, &stride, NULL, NULL, NULL));
    assert(buffer);
    ubase_assert(uref_pic_size(uref, &hsize, &vsize, NULL));
    hsize /= hsub;
    hsize *= macropixel_size;
    vsize /= vsub;
    for (int y = 0; y < vsize; y++) {
        for (int x = 0; x < hsize; x++)
            buffer[x] = value;
        buffer += stride;
    }
    uref_pic_plane_unmap(uref, chroma, 0, 0, -1, -1);
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

#define READ_PIXELS_10(a, b, c) \
    do { \
        uint32_t val = AV_RL32(src); \
        src++; \
        a = (val)       & 1023; \
        b = (val >> 10) & 1023; \
        c = (val >> 20) & 1023; \
    } while (0)

bool test_sucessful = false;

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref);
    upipe_dbg(upipe, "===> received input uref");

    const uint8_t *buffer;
    size_t w, h, stride;
    uint8_t wsub, hsub;

    ubase_assert(uref_pic_plane_read(uref, v210_chroma, 0, 0, -1, -1, &buffer));
    ubase_assert(uref_pic_plane_size(uref, v210_chroma, &stride, &wsub, &hsub, 0));
    ubase_assert(uref_pic_size(uref, &w, &h, 0));

    w /= wsub;
    h /= hsub;

    assert(w > 0);
    assert(h > 0);

    for (int y = 0; y < h; y++) {
        uint32_t *src = (uint32_t*)buffer;
        for (int x = 0; x < w-5; x += 6) {
            uint16_t a, b, c;
            READ_PIXELS_10(a, b, c);
            assert(a == (VALUE_U << 2));
            assert(b == (VALUE_Y << 2));
            assert(c == (VALUE_V << 2));

            READ_PIXELS_10(a, b, c);
            assert(a == (VALUE_Y << 2));
            assert(b == (VALUE_U << 2));
            assert(c == (VALUE_Y << 2));

            READ_PIXELS_10(a, b, c);
            assert(a == (VALUE_V << 2));
            assert(b == (VALUE_Y << 2));
            assert(c == (VALUE_U << 2));

            READ_PIXELS_10(a, b, c);
            assert(a == (VALUE_Y << 2));
            assert(b == (VALUE_V << 2));
            assert(c == (VALUE_Y << 2));

        }
        buffer += stride;
    }

    uref_pic_plane_unmap(uref, v210_chroma, 0, 0, -1, -1);
    uref_free(uref);
    test_sucessful = true;
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

    /* planar 4:2:2 */
    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc_fourcc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr, "YV16", -1, -1, -1, -1, 0, 0);
    assert(pic_mgr);

    /* allocate reference picture */
    struct uref *input_uref = uref_pic_alloc(uref_mgr, pic_mgr,
            TEST_WIDTH, TEST_HEIGHT);
    assert(input_uref);
    assert(input_uref->ubuf);

    /* fill reference picture */
    fill_in(input_uref, "y8", 1, 1, 1, VALUE_Y);
    fill_in(input_uref, "u8", 2, 1, 1, VALUE_U);
    fill_in(input_uref, "v8", 2, 1, 1, VALUE_V);

    /* create input flow definition */
    struct uref *in_flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(in_flow_def);

    ubase_assert(uref_pic_flow_add_plane(in_flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(in_flow_def, 2, 1, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(in_flow_def, 2, 1, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(in_flow_def, TEST_WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(in_flow_def, TEST_HEIGHT));

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
            UPROBE_LOG_LEVEL, "v210enc");
    struct uprobe *logger_test = uprobe_pfx_alloc(uprobe_use(logger),
            UPROBE_LOG_LEVEL, "test");
    assert(logger);

    /* build v210enc pipe */
    struct upipe_mgr *upipe_v210enc_mgr = upipe_v210enc_mgr_alloc();
    assert(upipe_v210enc_mgr);
    struct upipe *v210enc = upipe_void_alloc(upipe_v210enc_mgr, logger_v210);
    assert(v210enc);

    /* build phony pipe */
    struct upipe *test = upipe_void_alloc(&test_mgr, logger_test);
    assert(test);
    ubase_assert(upipe_set_output(v210enc, test));

    /* put the input flow definition into the pipe */
    ubase_assert(upipe_set_flow_def(v210enc, in_flow_def));

    /* send the picture */
    struct uref *pic = uref_dup(input_uref);
    assert(pic);
    upipe_input(v210enc, pic, 0);

    uref_free(in_flow_def);
    /* release v210enc pipe */
    uref_free(input_uref);
    upipe_release(v210enc);
    test_free(test);

    /* release managers */
    upipe_mgr_release(upipe_v210enc_mgr); // no-op
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return !test_sucessful;
}

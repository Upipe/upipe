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

/** @file
 * @short unit tests for SDI pack10bit module
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
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/ubuf_block_stream.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-hbrmt/upipe_pack10bit.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define UBUF_ALIGN 16 /* 128-bits simd */

#define WIDTH 1024

static bool received_block = false;

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
    assert(uref != NULL);
    size_t size;
    ubase_assert(uref_block_size(uref, &size));
    assert(size == WIDTH * 10 / 8);
    received_block = true;

    struct ubuf_block_stream s;
    ubase_assert(ubuf_block_stream_init(&s, uref->ubuf, 0));

    for (int i = 0; i < WIDTH; i++) {
        ubuf_block_stream_fill_bits(&s, 10);
        assert(ubuf_block_stream_show_bits(&s, 10) == i);
        ubuf_block_stream_skip_bits(&s, 10);
    }

    ubase_assert(ubuf_block_stream_clean(&s));

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

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, 0, 0,
                                                         UBUF_ALIGN, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    uprobe_stdio = uprobe_ubuf_mem_alloc(uprobe_stdio, umem_mgr,
            UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "");
    uref_block_flow_set_align(uref, UBUF_ALIGN);
    assert(uref != NULL);

    struct upipe_mgr *upipe_pack10bit_mgr = upipe_pack10bit_mgr_alloc();
    assert(upipe_pack10bit_mgr != NULL);
    struct upipe *upipe_pack10 = upipe_void_alloc(upipe_pack10bit_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "pack10"));
    assert(upipe_pack10 != NULL);
    ubase_assert(upipe_set_flow_def(upipe_pack10, uref));
    uref_free(uref);

    struct upipe *sink = upipe_void_alloc(&test_mgr, uprobe_use(uprobe_stdio));
    assert(sink != NULL);
    ubase_assert(upipe_set_output(upipe_pack10, sink));

    uint8_t *buffer;
    int size;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 2*WIDTH);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 2*WIDTH);
    uint16_t *pixels_buf = (uint16_t*)buffer;
    for (int i = 0; i < WIDTH; i++)
        pixels_buf[i] = i;
    uref_block_unmap(uref, 0);
    upipe_input(upipe_pack10, uref, NULL);
    assert(received_block);

    upipe_release(upipe_pack10);
    upipe_mgr_release(upipe_pack10bit_mgr); // nop

    test_free(sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);

    return 0;
}

/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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

/** @file
 * @short unit tests for A/52 audio framer module
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe.h>
#include <upipe-framers/upipe_a52_framer.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/atsc/a52.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static unsigned int nb_packets = 0;

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
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_SYNC_LOST:
            break;
        case UPROBE_NEW_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            uref_dump(flow_def, upipe->uprobe);
            break;
        }
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe to test upipe_a52f */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_a52f */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump *upump)
{
    assert(uref != NULL);
    upipe_dbg_va(upipe, "frame: %u", nb_packets);
    uref_dump(uref, upipe->uprobe);
    size_t size;
    assert(uref_block_size(uref, &size));
    uint64_t systime_rap = UINT64_MAX;
    uint64_t pts_orig = UINT64_MAX, dts_orig = UINT64_MAX;
    uref_clock_get_rap_sys(uref, &systime_rap);
    uref_clock_get_pts_orig(uref, &pts_orig);
    uref_clock_get_dts_orig(uref, &dts_orig);

    uint8_t *buffer = malloc(size);
    assert(buffer);
    uref_block_extract(uref, 0, size, buffer);

    assert(size == a52_get_frame_size(a52_get_fscod(buffer),
                                      a52_get_frmsizecod(buffer)));
    assert(systime_rap == 42);
    assert(pts_orig == 27000000);
    assert(dts_orig == 27000000);

    upipe_dbg_va(upipe, "frame size: %zu", size);
    free(buffer);
    uref_free(uref);
    nb_packets++;
}

/** helper phony pipe to test upipe_a52f */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_a52f */
static struct upipe_mgr test_mgr = {
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = NULL
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
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "");
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&test_mgr,
                                                uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_a52f_mgr = upipe_a52f_mgr_alloc();
    assert(upipe_a52f_mgr != NULL);
    struct upipe *upipe_a52f = upipe_void_alloc(upipe_a52f_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio),
                             UPROBE_LOG_LEVEL, "a52f"));
    assert(upipe_a52f != NULL);
    ubase_assert(upipe_set_flow_def(upipe_a52f, uref));
    ubase_assert(upipe_set_output(upipe_a52f, upipe_sink));
    uref_free(uref);

    uint8_t *buffer;
    int size;
    int framesize = a52_get_frame_size(A52_FSCOD_48KHZ, 10);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 4*framesize);
    assert(uref);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 4*framesize);
    memset(buffer, 0, size);

    int i;
    for (i=0; i < 4; i++) {
        a52_set_sync(buffer);
        a52_set_fscod(buffer, A52_FSCOD_48KHZ);
        a52_set_frmsizecod(buffer, 10);
        a52_set_bsid(buffer, A52_BSID);
        buffer += framesize;
    }

    uref_block_unmap(uref, 0);
    uref_clock_set_pts_orig(uref, 27000000);
    uref_clock_set_dts_orig(uref, 27000000);
    uref_clock_set_cr_sys(uref, 84);
    uref_clock_set_rap_sys(uref, 42);
    upipe_input(upipe_a52f, uref, NULL);
    assert(nb_packets == 4);

    upipe_release(upipe_a52f);

    upipe_mgr_release(upipe_a52f_mgr);

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}

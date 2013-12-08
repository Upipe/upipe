/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
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
 * @short unit tests for TS sync module
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
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_sync.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/mpeg/ts.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static unsigned int nb_packets = 0;
static int expect_loss = -1;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(event & UPROBE_HANDLED_FLAG);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_SYNC_ACQUIRED:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_SYNC_LOST:
            assert(expect_loss == nb_packets);
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_ts_sync */
static struct upipe *ts_test_alloc(struct upipe_mgr *mgr,
                                   struct uprobe *uprobe, uint32_t signature,
                                   va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_ts_sync */
static void ts_test_input(struct upipe *upipe, struct uref *uref,
                          struct upump *upump)
{
    assert(uref != NULL);
    size_t size;
    assert(uref_block_size(uref, &size));
    assert(size == TS_SIZE);

    const uint8_t *buffer;
    int rsize = 1;
    assert(uref_block_read(uref, 0, &rsize, &buffer));
    assert(rsize == 1);
    assert(ts_validate(buffer));
    uref_block_unmap(uref, 0);
    uref_free(uref);
    nb_packets--;
}

/** helper phony pipe to test upipe_ts_sync */
static void ts_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_ts_sync */
static struct upipe_mgr ts_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = ts_test_alloc,
    .upipe_input = ts_test_input,
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
    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&ts_test_mgr, uprobe_stdio);
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_ts_sync_mgr = upipe_ts_sync_mgr_alloc();
    assert(upipe_ts_sync_mgr != NULL);
    struct upipe *upipe_ts_sync = upipe_void_alloc(upipe_ts_sync_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_stdio, UPROBE_LOG_LEVEL, "ts sync"));
    assert(upipe_ts_sync != NULL);
    assert(upipe_set_flow_def(upipe_ts_sync, uref));
    assert(upipe_set_output(upipe_ts_sync, upipe_sink));
    uref_free(uref);

    uint8_t *buffer;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 2 * TS_SIZE);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 2 * TS_SIZE);
    ts_pad(buffer);
    ts_pad(buffer + TS_SIZE);
    uref_block_unmap(uref, 0);
    nb_packets++;
    upipe_input(upipe_ts_sync, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 2 * TS_SIZE + 12);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == 2 * TS_SIZE + 12);
    buffer[0] = 0x47;
    memset(buffer + 1, 0, 11);
    ts_pad(buffer + 12);
    ts_pad(buffer + 12 + TS_SIZE);
    uref_block_unmap(uref, 0);
    nb_packets += 2;
    expect_loss = 1;
    upipe_input(upipe_ts_sync, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE / 2);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE / 2);
    buffer[0] = 0x47;
    memset(buffer + 1, 0, TS_SIZE / 2 - 1);
    uref_block_unmap(uref, 0);
    nb_packets++; // because this will release the previous packet
    upipe_input(upipe_ts_sync, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, TS_SIZE / 2);
    assert(uref != NULL);
    size = -1;
    assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == TS_SIZE / 2);
    memset(buffer, 0, TS_SIZE / 2);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_sync, uref, NULL);
    assert(!nb_packets);

    nb_packets++;
    upipe_release(upipe_ts_sync);
    assert(!nb_packets);
    upipe_mgr_release(upipe_ts_sync_mgr); // nop

    ts_test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}

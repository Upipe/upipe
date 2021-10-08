/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short unit tests for htons module
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block_mem.h"
#include "upipe/uref.h"
#include "upipe/uref_block_flow.h"
#include "upipe/uref_block.h"
#include "upipe/uref_std.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_htons.h"

#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

#define PACKETS_NUM 45
#define PACKET_SIZE 524

static unsigned int nb_packets = 0;

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
static struct upipe *test_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
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
    const uint8_t *buffer;
    size_t size = 0;
    int pos = 0, len = -1;
    ubase_assert(uref_block_size(uref, &size));
    upipe_dbg_va(upipe, "received packet of size %zu", size);

    while (size > 0) {
        ubase_assert(uref_block_read(uref, pos, &len, &buffer));
        pos += len;
        while (len > 1) {
            assert(htons(*(uint16_t*)buffer) == nb_packets*PACKET_SIZE+size);
            len -= 2;
            size -= 2;
            buffer += 2;
        }
        uref_block_unmap(uref, 0);
    }
    nb_packets--;
    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
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
static struct upipe_mgr htons_test_mgr = {
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
                                                         umem_mgr, 0, 0, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    /* flow def */
    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "foo.");
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&htons_test_mgr,
                                                uprobe_use(uprobe_stdio));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_htons_mgr = upipe_htons_mgr_alloc();
    assert(upipe_htons_mgr != NULL);
    struct upipe *upipe_htons = upipe_void_alloc(upipe_htons_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "htons"));
    assert(upipe_htons != NULL);
    ubase_assert(upipe_set_flow_def(upipe_htons, uref));
    ubase_assert(upipe_set_output(upipe_htons, upipe_sink));
    uref_free(uref);

    uint8_t *buffer;
    int size, i;

    nb_packets = PACKETS_NUM;
    for (i=0; i < PACKETS_NUM; i++) {
        uref = uref_block_alloc(uref_mgr, ubuf_mgr, PACKET_SIZE);
        size = -1;
        uref_block_write(uref, 0, &size, &buffer);
        assert(size == PACKET_SIZE);
        while (size > 1) {
            *(uint16_t*)buffer = nb_packets*PACKET_SIZE+size;
            buffer += 2;
            size -= 2;
        }
        uref_block_unmap(uref, 0);
        upipe_input(upipe_htons, uref, NULL);
    }
    /* TODO test segmented, shared, unaligned ubufs */

    /* flush */
    upipe_release(upipe_htons);

    printf("nb_packets: %u\n", nb_packets);
    assert(nb_packets == 0);

    /* release everything */
    upipe_mgr_release(upipe_htons_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}

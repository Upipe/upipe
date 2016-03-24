/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short unit tests for aggregate module
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
#include <upipe-modules/upipe_aggregate.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

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

/** helper phony pipe to test upipe_agg */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_agg */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    size_t size = 0;
    ubase_assert(uref_block_size(uref, &size));
    assert(!nb_packets ? size == 376 : size == 188);

    nb_packets++;
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

/** helper phony pipe to test upipe_agg */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_agg */
static struct upipe_mgr aggregate_test_mgr = {
    .refcount = NULL,
    .signature = 0,
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
                                                         umem_mgr, -1, 0);
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

    struct upipe_mgr *upipe_agg_mgr = upipe_agg_mgr_alloc();
    assert(upipe_agg_mgr != NULL);
    struct upipe *upipe_agg = upipe_void_alloc(upipe_agg_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "aggregate"));
    assert(upipe_agg != NULL);
    ubase_assert(upipe_set_flow_def(upipe_agg, uref));

    uref_free(uref);
    ubase_assert(upipe_get_flow_def(upipe_agg, &uref));
    const char *def;
    ubase_assert(uref_flow_get_def(uref, &def));
    assert(!strcmp(def, "block.foo."));

    struct upipe *upipe_sink = upipe_void_alloc(&aggregate_test_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "test"));
    assert(upipe_sink != NULL);
    ubase_assert(upipe_set_output(upipe_agg, upipe_sink));
    upipe_release(upipe_sink);

    ubase_assert(upipe_set_output_size(upipe_agg, 376));

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 188);
    upipe_input(upipe_agg, uref, NULL);
    assert(!nb_packets);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 188);
    upipe_input(upipe_agg, uref, NULL);
    assert(nb_packets == 1);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 188);
    upipe_input(upipe_agg, uref, NULL);
    assert(nb_packets == 1);

    /* flush */
    upipe_release(upipe_agg);

    assert(nb_packets == 2);

    /* release everything */
    upipe_mgr_release(upipe_agg_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}

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
 * @short unit tests for dup pipes
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
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_dup.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static int counter = 0;

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
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_SOURCE_END:
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_dup */
static struct upipe *dup_test_alloc(struct upipe_mgr *mgr,
                                    struct uprobe *uprobe,
                                    uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_dup */
static void dup_test_input(struct upipe *upipe, struct uref *uref,
                           struct upump *upump)
{
    assert(uref != NULL);
    counter++;
    uref_free(uref);
}

/** helper phony pipe to test upipe_dup */
static void dup_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr dup_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = dup_test_alloc,
    .upipe_input = dup_test_input,
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
    struct uref *uref;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    struct upipe *upipe_sink0 = upipe_void_alloc(&dup_test_mgr, logger);
    assert(upipe_sink0 != NULL);

    struct upipe *upipe_sink1 = upipe_void_alloc(&dup_test_mgr, logger);
    assert(upipe_sink1 != NULL);

    uref = uref_block_flow_alloc_def(uref_mgr, "foo.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_dup_mgr = upipe_dup_mgr_alloc();
    assert(upipe_dup_mgr != NULL);
    struct upipe *upipe_dup = upipe_void_alloc(upipe_dup_mgr,
            uprobe_pfx_adhoc_alloc(logger, UPROBE_LOG_LEVEL, "dup"));
    assert(upipe_dup != NULL);
    assert(upipe_set_flow_def(upipe_dup, uref));
    uref_free(uref);

    struct upipe *upipe_dup_output0 = upipe_void_alloc_sub(upipe_dup,
            uprobe_pfx_adhoc_alloc(logger, UPROBE_LOG_LEVEL, "dup output 0"));
    assert(upipe_dup_output0 != NULL);
    assert(upipe_set_output(upipe_dup_output0, upipe_sink0));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_dup, uref, NULL);
    assert(counter == 1);
    counter = 0;

    struct upipe *upipe_dup_output1 = upipe_void_alloc_sub(upipe_dup,
            uprobe_pfx_adhoc_alloc(logger, UPROBE_LOG_LEVEL,
                                   "dup output 1"));
    assert(upipe_dup_output1 != NULL);
    assert(upipe_set_output(upipe_dup_output1, upipe_sink1));
    assert(counter == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_dup, uref, NULL);
    assert(counter == 2);

    upipe_release(upipe_dup);
    upipe_release(upipe_dup_output0);
    upipe_release(upipe_dup_output1);
    upipe_mgr_release(upipe_dup_mgr); // nop

    dup_test_free(upipe_sink0);
    dup_test_free(upipe_sink1);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    uprobe_stdio_free(logger);
    return 0;
}

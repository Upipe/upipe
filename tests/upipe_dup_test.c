/*****************************************************************************
 * upipe_dup_test.c: unit tests for dup pipes
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_std.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_dup.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UREF_POOL_DEPTH 10
#define ULOG_LEVEL ULOG_DEBUG

static int counter = 0;

/** definition of our struct uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
{
    switch (event) {
        case UPROBE_AERROR:
        case UPROBE_UPUMP_ERROR:
        case UPROBE_READ_END:
        case UPROBE_WRITE_END:
        case UPROBE_NEW_FLOW:
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_LINEAR_NEED_UBUF_MGR:
        case UPROBE_SOURCE_NEED_FLOW_NAME:
        default:
            assert(0);
            break;
        case UPROBE_READY:
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_dup */
struct dup_test {
    const char *flow;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_dup */
static struct upipe *dup_test_alloc(struct upipe_mgr *mgr)
{
    struct dup_test *dup_test = malloc(sizeof(struct dup_test));
    if (unlikely(dup_test == NULL)) return NULL;
    dup_test->flow = NULL;
    dup_test->upipe.mgr = mgr;
    return &dup_test->upipe;
}

/** helper phony pipe to test upipe_dup */
static void dup_test_set_flow(struct upipe *upipe, const char *flow)
{
    struct dup_test *dup_test = container_of(upipe, struct dup_test, upipe);
    dup_test->flow = flow;
}

/** helper phony pipe to test upipe_dup */
static bool dup_test_control(struct upipe *upipe, enum upipe_control control,
                             va_list args)
{
    if (likely(control == UPIPE_INPUT)) {
        struct dup_test *dup_test = container_of(upipe, struct dup_test, upipe);
        struct uref *uref = va_arg(args, struct uref *);
        assert(uref != NULL);
        const char *flow;
        assert(uref_flow_get_name(uref, &flow));
        assert(!strcmp(flow, dup_test->flow));
        counter++;
        uref_release(uref);
        return true;
    }
    return false;
}

/** helper phony pipe to test upipe_dup */
static void dup_test_free(struct upipe *upipe)
{
    struct dup_test *dup_test = container_of(upipe, struct dup_test, upipe);
    free(dup_test);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr dup_test_mgr = {
    .upipe_alloc = dup_test_alloc,
    .upipe_control = dup_test_control,
    .upipe_free = dup_test_free,

    .upipe_mgr_free = NULL
};

int main(int argc, char *argv[])
{
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, -1, -1);
    struct uref *uref;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_print = uprobe_print_alloc(&uprobe, stdout, "test");
    assert(uprobe_print != NULL);

    struct upipe *upipe_sink0 = upipe_alloc(&dup_test_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "sink 0"));
    assert(upipe_sink0 != NULL);
    dup_test_set_flow(upipe_sink0, "source.0");

    struct upipe *upipe_sink1 = upipe_alloc(&dup_test_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "sink 1"));
    assert(upipe_sink1 != NULL);
    dup_test_set_flow(upipe_sink1, "source.1");

    struct upipe_mgr *upipe_dup_mgr = upipe_dup_mgr_alloc();
    assert(upipe_dup_mgr != NULL);
    struct upipe *upipe_dup = upipe_alloc(upipe_dup_mgr, uprobe_print,
            ulog_std_alloc(stdout, ULOG_LEVEL, "dup"));
    assert(upipe_dup != NULL);
    assert(upipe_set_uref_mgr(upipe_dup, uref_mgr));
    assert(upipe_split_set_output(upipe_dup, upipe_sink0, "0"));

    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);
    assert(uref_flow_set_name(&uref, "source"));
    upipe_input(upipe_dup, uref);
    assert(counter == 1);
    counter = 0;

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_name(&uref, "source"));
    upipe_input(upipe_dup, uref);
    assert(counter == 1);
    counter = 0;

    assert(upipe_split_set_output(upipe_dup, upipe_sink1, "1"));
    assert(counter == 1);
    counter = 0;

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_name(&uref, "source"));
    upipe_input(upipe_dup, uref);
    assert(counter == 2);
    counter = 0;

    uref = uref_flow_alloc_delete(uref_mgr, "source");
    assert(uref != NULL);
    upipe_input(upipe_dup, uref);
    assert(counter == 2);

    assert(urefcount_single(&upipe_dup->refcount));
    upipe_release(upipe_dup);
    upipe_mgr_release(upipe_dup_mgr); // nop

    assert(urefcount_single(&upipe_sink0->refcount));
    upipe_release(upipe_sink0);
    assert(urefcount_single(&upipe_sink1->refcount));
    upipe_release(upipe_sink1);

    assert(urefcount_single(&uref_mgr->refcount));
    uref_mgr_release(uref_mgr);

    uprobe_print_free(uprobe_print);
    return 0;
}

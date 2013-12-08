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
 * @short unit tests for queue source and sink pipes
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
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_queue_source.h>
#include <upipe-modules/upipe_queue_sink.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define QUEUE_LENGTH 6
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

UREF_ATTR_SMALL_UNSIGNED(test, test, "x.test", test)

static struct ev_loop *loop;
static struct upump_mgr *upump_mgr;
static struct upipe *upipe_qsink;
static uint8_t counter = 0;

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
        case UPROBE_NEED_UREF_MGR:
        case UPROBE_NEED_UPUMP_MGR:
        case UPROBE_NEW_FLOW_DEF:
            break;
        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            break;
    }
    return true;
}

/** helper phony pipe to test upipe_qsrc */
static struct upipe *queue_test_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe, uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** helper phony pipe to test upipe_qsrc */
static void queue_test_input(struct upipe *upipe, struct uref *uref,
                             struct upump *upump)
{
    assert(uref != NULL);
    upipe_notice_va(upipe, "loop %"PRIu8, counter);
    if (counter == 0) {
        uint8_t uref_counter;
        assert(uref_test_get_test(uref, &uref_counter));
        assert(uref_counter == counter);
    } else
        upipe_release(upipe_qsink);
    counter++;
    uref_free(uref);
}

/** helper phony pipe to test upipe_qsrc */
static void queue_test_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_qsrc */
static struct upipe_mgr queue_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = queue_test_alloc,
    .upipe_input = queue_test_input,
    .upipe_control = NULL
};

int main(int argc, char *argv[])
{
    loop = ev_default_loop(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);

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
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);

    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&queue_test_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_stdio, UPROBE_LOG_LEVEL, "sink"));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    assert(upipe_qsrc_mgr != NULL);
    struct upipe *upipe_qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_stdio, UPROBE_LOG_LEVEL, "queue source"),
            QUEUE_LENGTH);
    assert(upipe_qsrc != NULL);
    assert(upipe_set_upump_mgr(upipe_qsrc, upump_mgr));
    assert(upipe_set_output(upipe_qsrc, upipe_sink));

    struct upipe_mgr *upipe_qsink_mgr = upipe_qsink_mgr_alloc();
    assert(upipe_qsink_mgr != NULL);
    upipe_qsink = upipe_void_alloc(upipe_qsink_mgr,
            uprobe_pfx_adhoc_alloc(uprobe_stdio, UPROBE_LOG_LEVEL, "queue sink"));
    assert(upipe_qsink != NULL);
    assert(upipe_set_flow_def(upipe_qsink, uref));
    assert(upipe_set_upump_mgr(upipe_qsink, upump_mgr));
    assert(upipe_qsink_set_qsrc(upipe_qsink, upipe_qsrc));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_test_set_test(uref, 0));
    upipe_input(upipe_qsink, uref, NULL);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_test_set_test(uref, 1));
    upipe_input(upipe_qsink, uref, NULL);

    unsigned int length;
    assert(upipe_qsrc_get_length(upipe_qsrc, &length));
    assert(length == 3);

    ev_loop(loop, 0);

    assert(counter == 2);

    upipe_mgr_release(upipe_qsink_mgr); // nop
    upipe_mgr_release(upipe_qsrc_mgr); // nop

    queue_test_free(upipe_sink);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_stdio_free(uprobe_stdio);

    ev_default_destroy();
    return 0;
}

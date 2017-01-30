/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
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
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
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

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define QUEUE_LENGTH 6
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

UREF_ATTR_SMALL_UNSIGNED(test, test, "x.test", test)

static struct upump_mgr *upump_mgr;
static struct upipe *upipe_qsink;
static uint8_t counter = 0;
static struct uref_mgr *uref_mgr;
static struct urequest request;
static bool request_was_unregistered = false;

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
        case UPROBE_SOURCE_END:
            upipe_release(upipe);
            break;
    }
    return UBASE_ERR_NONE;
}

static void check_end(void)
{
    if (counter >= 1 && request_was_unregistered)
        upipe_release(upipe_qsink);
}

static int provide_request(struct urequest *urequest, va_list args)
{
    upipe_notice(upipe_qsink, "providing request");
    assert(urequest == &request);
    struct uref_mgr *m = va_arg(args, struct uref_mgr *);
    assert(m == uref_mgr);
    uref_mgr_release(uref_mgr);
    upipe_unregister_request(upipe_qsink, urequest);
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_ready(upipe);
    return upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref != NULL);
    upipe_notice_va(upipe, "loop %"PRIu8, counter);
    if (counter == 0) {
        uint8_t uref_counter;
        ubase_assert(uref_test_get_test(uref, &uref_counter));
        assert(uref_counter == counter);
    } else
        check_end();
    counter++;
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
            request_was_unregistered = true;
            check_end();
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe */
static struct upipe_mgr queue_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char *argv[])
{
    upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);
    struct uref *uref;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);

    uref = uref_block_flow_alloc_def(uref_mgr, NULL);
    assert(uref != NULL);

    struct upipe *upipe_sink = upipe_void_alloc(&queue_test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "sink"));
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_qsrc_mgr = upipe_qsrc_mgr_alloc();
    assert(upipe_qsrc_mgr != NULL);
    struct upipe *upipe_qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "queue source"), QUEUE_LENGTH);
    assert(upipe_qsrc != NULL);
    ubase_assert(upipe_set_output(upipe_qsrc, upipe_sink));

    struct upipe_mgr *upipe_qsink_mgr = upipe_qsink_mgr_alloc();
    assert(upipe_qsink_mgr != NULL);
    upipe_qsink = upipe_qsink_alloc(upipe_qsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "queue sink"),
            upipe_qsrc);
    assert(upipe_qsink != NULL);
    ubase_assert(upipe_set_flow_def(upipe_qsink, uref));
    uref_free(uref);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_test_set_test(uref, 0));
    upipe_input(upipe_qsink, uref, NULL);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_test_set_test(uref, 1));
    upipe_input(upipe_qsink, uref, NULL);

    unsigned int length;
    ubase_assert(upipe_qsrc_get_length(upipe_qsrc, &length));
    assert(length == 3);

    urequest_init_uref_mgr(&request, provide_request, NULL);
    upipe_register_request(upipe_qsink, &request);

    upump_mgr_run(upump_mgr, NULL);

    assert(counter == 2);
    assert(request_was_unregistered);

    /* check that they are correctly released even if no flow def is input */
    upipe_qsrc = upipe_qsrc_alloc(upipe_qsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "queue source"), QUEUE_LENGTH);
    assert(upipe_qsrc != NULL);

    upipe_qsink = upipe_qsink_alloc(upipe_qsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "queue sink"),
            upipe_qsrc);
    assert(upipe_qsink != NULL);
    upipe_release(upipe_qsrc);
    upipe_release(upipe_qsink);

    upipe_mgr_release(upipe_qsink_mgr); // nop
    upipe_mgr_release(upipe_qsrc_mgr); // nop

    test_free(upipe_sink);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

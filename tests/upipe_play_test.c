/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for play pipes
 */

#undef NDEBUG

#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/urequest.h>
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
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_play.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static struct urequest *request;

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
        case UPROBE_SOURCE_END:
        case UPROBE_PROVIDE_REQUEST:
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
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_REGISTER_REQUEST:
            request = va_arg(args, struct urequest *);
            return UBASE_ERR_NONE;
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
    .upipe_input = NULL,
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
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    struct upipe_mgr *upipe_play_mgr = upipe_play_mgr_alloc();
    assert(upipe_play_mgr != NULL);
    struct upipe *upipe_play = upipe_void_alloc(upipe_play_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "play"));
    assert(upipe_play != NULL);

    struct upipe *upipe_play1 = upipe_void_alloc_sub(upipe_play,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "play 1"));
    assert(upipe_play1 != NULL);

    struct upipe *test_sink1 = upipe_void_alloc_output(upipe_play1, &test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "sink 1"));
    assert(test_sink1 != NULL);

    struct upipe *upipe_play2 = upipe_void_alloc_sub(upipe_play,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "play 2"));
    assert(upipe_play2 != NULL);

    struct upipe *test_sink2 = upipe_void_alloc_output(upipe_play2, &test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "sink 2"));
    assert(test_sink2 != NULL);

    struct uref *input_flow_def = uref_alloc(uref_mgr);
    assert(input_flow_def != NULL);
    ubase_assert(uref_flow_set_def(input_flow_def, "void."));
    ubase_assert(uref_clock_set_latency(input_flow_def, UCLOCK_FREQ));
    ubase_assert(upipe_set_flow_def(upipe_play1, input_flow_def));
    ubase_assert(urequest_provide_sink_latency(request, 0));

    struct uref *output_flow_def;
    ubase_assert(upipe_get_flow_def(upipe_play1, &output_flow_def));
    uint64_t latency;
    ubase_assert(uref_clock_get_latency(output_flow_def, &latency));
    assert(latency == UCLOCK_FREQ + UCLOCK_FREQ / 50);

    ubase_assert(uref_clock_set_latency(input_flow_def, UCLOCK_FREQ * 2));
    ubase_assert(upipe_set_flow_def(upipe_play2, input_flow_def));

    ubase_assert(upipe_get_flow_def(upipe_play1, &output_flow_def));
    ubase_assert(uref_clock_get_latency(output_flow_def, &latency));
    assert(latency == UCLOCK_FREQ * 2 + UCLOCK_FREQ / 50);

    ubase_assert(urequest_provide_sink_latency(request, UCLOCK_FREQ));

    ubase_assert(upipe_get_flow_def(upipe_play1, &output_flow_def));
    ubase_assert(uref_clock_get_latency(output_flow_def, &latency));
    assert(latency == UCLOCK_FREQ * 3);

    ubase_assert(upipe_get_flow_def(upipe_play2, &output_flow_def));
    ubase_assert(uref_clock_get_latency(output_flow_def, &latency));
    assert(latency == UCLOCK_FREQ * 3);

    uref_free(input_flow_def);

    upipe_release(upipe_play);
    upipe_release(upipe_play1);
    upipe_release(upipe_play2);
    upipe_mgr_release(upipe_play_mgr); // nop

    test_free(test_sink1);
    test_free(test_sink2);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    uprobe_release(logger);
    uprobe_clean(&uprobe);
    return 0;
}

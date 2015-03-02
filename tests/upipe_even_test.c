/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for even pipes
 */

#undef NDEBUG

#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_even.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static unsigned int count_pic = 0;
static unsigned int count_sound = 0;
static unsigned int count_subpic = 0;

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
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
struct test_pipe {
    unsigned int *count_p;
    struct upipe upipe;
};

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct test_pipe *test_pipe = malloc(sizeof(struct test_pipe));
    assert(test_pipe != NULL);
    upipe_init(&test_pipe->upipe, mgr, uprobe);
    struct uref *flow_def = va_arg(args, struct uref *);
    const char *def;
    ubase_assert(uref_flow_get_def(flow_def, &def));
    if (!strcmp(def, "pic."))
        test_pipe->count_p = &count_pic;
    else if (!strcmp(def, "sound.s16."))
        test_pipe->count_p = &count_sound;
    else
        test_pipe->count_p = &count_subpic;
    return &test_pipe->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct test_pipe *test_pipe = container_of(upipe, struct test_pipe, upipe);
    assert(uref != NULL);
    (*test_pipe->count_p)++;
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
    struct test_pipe *test_pipe = container_of(upipe, struct test_pipe, upipe);
    upipe_clean(upipe);
    free(test_pipe);
}

/** helper phony pipe */
static struct upipe_mgr even_test_mgr = {
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
    struct uref *uref;
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);

    struct upipe_mgr *upipe_even_mgr = upipe_even_mgr_alloc();
    assert(upipe_even_mgr != NULL);
    struct upipe *upipe_even = upipe_void_alloc(upipe_even_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "even"));
    assert(upipe_even != NULL);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "pic."));

    struct upipe *upipe_sink_pic = upipe_flow_alloc(&even_test_mgr,
                                                    uprobe_use(logger), uref);
    assert(upipe_sink_pic != NULL);

    struct upipe *upipe_even_pic = upipe_void_alloc_sub(upipe_even,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "even pic"));
    assert(upipe_even_pic != NULL);
    ubase_assert(upipe_set_flow_def(upipe_even_pic, uref));
    assert(upipe_even_pic != NULL);
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_even_pic, upipe_sink_pic));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "sound.s16."));

    struct upipe *upipe_sink_sound = upipe_flow_alloc(&even_test_mgr,
                                                      uprobe_use(logger), uref);
    assert(upipe_sink_sound != NULL);

    struct upipe *upipe_even_sound = upipe_void_alloc_sub(upipe_even,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "even sound"));
    assert(upipe_even_sound != NULL);
    ubase_assert(upipe_set_flow_def(upipe_even_sound, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_even_sound, upipe_sink_sound));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "pic.sub."));

    struct upipe *upipe_sink_subpic = upipe_flow_alloc(&even_test_mgr,
                                                       uprobe_use(logger),
                                                       uref);
    assert(upipe_sink_subpic != NULL);

    struct upipe *upipe_even_subpic = upipe_void_alloc_sub(upipe_even,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "even subpic"));
    assert(upipe_even_subpic != NULL);
    ubase_assert(upipe_set_flow_def(upipe_even_subpic, uref));
    uref_free(uref);
    ubase_assert(upipe_set_output(upipe_even_subpic, upipe_sink_subpic));

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_sys(uref, (uint64_t)UINT32_MAX);
    uref_clock_set_duration(uref, 10);
    upipe_input(upipe_even_pic, uref, NULL);
    assert(count_pic == 0);
    assert(count_sound == 0);
    assert(count_subpic == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_sys(uref, (uint64_t)UINT32_MAX * 2);
    uref_clock_set_duration(uref, 10);
    upipe_input(upipe_even_sound, uref, NULL);
    assert(count_pic == 0);
    assert(count_sound == 0);
    assert(count_subpic == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_sys(uref, (uint64_t)UINT32_MAX * 2 + 1);
    uref_clock_set_duration(uref, 10);
    upipe_input(upipe_even_pic, uref, NULL);
    assert(count_pic == 1);
    assert(count_sound == 1);
    assert(count_subpic == 0);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_sys(uref, (uint64_t)UINT32_MAX * 2);
    upipe_input(upipe_even_subpic, uref, NULL);
    assert(count_pic == 1);
    assert(count_sound == 1);
    assert(count_subpic == 1);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    uref_clock_set_pts_sys(uref, (uint64_t)UINT32_MAX * 3);
    upipe_input(upipe_even_pic, uref, NULL);
    assert(count_pic == 1);
    assert(count_sound == 1);
    assert(count_subpic == 1);

    upipe_release(upipe_even);
    upipe_release(upipe_even_pic);
    upipe_release(upipe_even_sound);
    upipe_release(upipe_even_subpic);
    upipe_mgr_release(upipe_even_mgr); // nop

    test_free(upipe_sink_pic);
    test_free(upipe_sink_sound);
    test_free(upipe_sink_subpic);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);

    uprobe_release(logger);
    uprobe_clean(&uprobe);
    return 0;
}

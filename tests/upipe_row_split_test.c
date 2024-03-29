/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd.
 *
 * Authors: Rafaël Carré
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
 * @short unit tests for upipe row_split
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/uclock_std.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref_dump.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upump-ev/upump_ev.h"

#include "upipe-modules/upipe_blank_source.h"
#include "upipe-modules/upipe_row_split.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define UPUMP_POOL          1
#define UPUMP_BLOCKER_POOL  1
#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define WIDTH               96
#define HEIGHT              64
#define CHUNK_HEIGHT        8
#define LIMIT               10*CHUNK_HEIGHT
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE

/* fps */
static const struct urational fps = { .num = 25, .den = 1 };

/** blank source */
struct upipe *blksrc;

struct uref_mgr *uref_mgr;

/** phony pipe to test upipe_blksrc */
struct row_split_test {
    int counter;
    struct upipe upipe;
    uint64_t next_pts;
    uint64_t next_vpos;
};

/** helper phony pipe */
UPIPE_HELPER_UPIPE(row_split_test, upipe, 0);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct row_split_test *row_split_test = malloc(sizeof(struct row_split_test));
    assert(row_split_test != NULL);
    upipe_init(&row_split_test->upipe, mgr, uprobe);
    row_split_test->counter = 0;
    row_split_test->next_pts = UINT64_MAX;
    row_split_test->next_vpos = 0;
    upipe_throw_ready(&row_split_test->upipe);
    return &row_split_test->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct row_split_test *row_split_test = row_split_test_from_upipe(upipe);
    uint64_t pts = 0, duration = 0, vpos = 0;
    uref_dump(uref, upipe->uprobe);
    ubase_assert(uref_clock_get_pts_sys(uref, &pts));
    ubase_assert(uref_clock_get_duration(uref, &duration));
    ubase_assert(uref_pic_get_vposition(uref, &vpos));

    if (unlikely(row_split_test->next_pts == UINT64_MAX)) {
        row_split_test->next_pts = pts;
    }

    assert(pts == row_split_test->next_pts);
    assert(vpos == row_split_test->next_vpos);

    row_split_test->next_pts += duration;
    row_split_test->next_vpos += CHUNK_HEIGHT;
    if (row_split_test->next_vpos == HEIGHT)
        row_split_test->next_vpos = 0;
    row_split_test->counter++;
    uref_free(uref);

    if (unlikely(++row_split_test->counter > LIMIT)) {
        upipe_release(blksrc);
        blksrc = NULL;
    }
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
            return upipe_control_provide_request(upipe, command, args);
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    struct row_split_test *row_split_test = row_split_test_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(row_split_test);
}

/** helper phony pipe */
static struct upipe_mgr row_split_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

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

int main(int argc, char **argv)
{
    printf("Compiled %s %s (%s)\n", __DATE__, __TIME__, __FILE__);

    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    /* upipe env */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                                     UBUF_POOL_DEPTH);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_uclock_alloc(logger, uclock);
    assert(logger != NULL);

    /* blksrc manager */
    struct upipe_mgr *upipe_blksrc_mgr = upipe_blksrc_mgr_alloc();

    /* row_split manager */
    struct upipe_mgr *upipe_row_split_mgr = upipe_row_split_mgr_alloc();

    /* test pipe */
    struct upipe *row_split_test;

    /* pic flow definition */
    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow);
    ubase_assert(uref_pic_flow_add_plane(flow, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow, WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(flow, HEIGHT));
    ubase_assert(uref_pic_flow_set_fps(flow, fps));

    /* blksrc pipe */
    blksrc = upipe_flow_alloc(upipe_blksrc_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "blksrc"),
        flow);
    assert(blksrc);
    uref_free(flow);

    /* row_split pipe */
    flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow);
    ubase_assert(uref_pic_flow_set_vsize(flow, CHUNK_HEIGHT));
    struct upipe *row_split = upipe_flow_alloc_output(blksrc, upipe_row_split_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "row_split"),
        flow);
    assert(row_split);
    uref_free(flow);

    /* row_split_test */
    row_split_test = upipe_void_chain_output(row_split, &row_split_test_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "row_split_test"));
    assert(row_split_test);

    /* launch test */
    upump_mgr_run(upump_mgr, NULL);

    /* release pipes */
    test_free(row_split_test);

    /* clean everything */
    upipe_mgr_release(upipe_blksrc_mgr); // noop
    upipe_mgr_release(upipe_row_split_mgr); // noop
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);
    uclock_release(uclock);

    return 0;
}

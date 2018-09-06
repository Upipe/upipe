/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd.
 *
 * Authors: James Darnley
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
 * @short unit tests for upipe row_join
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/uclock_std.h>
#include <upipe/ubuf.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref_dump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upump-ev/upump_ev.h>

#include <upipe-modules/upipe_blank_source.h>
#include <upipe-modules/upipe_row_join.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_setflowdef.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UPUMP_POOL          1
#define UPUMP_BLOCKER_POOL  1
#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define WIDTH               96
#define HEIGHT              64
#define CHUNK_HEIGHT        8
#define UPROBE_LOG_LEVEL    UPROBE_LOG_VERBOSE

/** blank source */
struct upipe *blksrc;

/*
 * testing pipe
 */

struct test {
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(test, upipe, 0);

static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct test *ctx = malloc(sizeof(struct test));
    assert(ctx);
    upipe_init(&ctx->upipe, mgr, uprobe);
    upipe_throw_ready(&ctx->upipe);
    return &ctx->upipe;
}

static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    assert(uref_pic_get_vposition(uref, NULL));

    size_t width, height;
    ubase_assert(uref_pic_size(uref, &width, &height, NULL));
    assert(width == WIDTH && height == HEIGHT);

    uref_free(uref);
    upipe_release(blksrc);
}

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

static void test_free(struct upipe *upipe)
{
    struct test *ctx = test_from_upipe(upipe);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(ctx);
}

static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

/*
 * modify probe
 */

static int catch_blksrc_urefs(struct uprobe *uprobe, struct upipe *upipe,
        int event, va_list args)
{
    static int vpos = 0;
    if (event != UPROBE_PROBE_UREF) {
        return uprobe_throw_next(uprobe, upipe, event, args);
    }

    UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE);
    struct uref *uref = va_arg(args, struct uref *);
    assert(uref);
    upipe_dbg_va(upipe, "setting vposition to %d", vpos);
    UBASE_RETURN(uref_pic_set_vposition(uref, vpos));
    vpos = (vpos + CHUNK_HEIGHT) % HEIGHT;
    return UBASE_ERR_NONE;
}

/*
 * main probe
 */

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
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);

    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(NULL, stdout,
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

    /* specific probes */
    struct uprobe uprobe_modify_blksrc_urefs;
    uprobe_init(&uprobe_modify_blksrc_urefs, catch_blksrc_urefs, uprobe_use(logger));

    /* blksrc manager */
    struct upipe_mgr *blksrc_mgr = upipe_blksrc_mgr_alloc();

    /* setflowdef manager */
    struct upipe_mgr *setflowdef_mgr = upipe_setflowdef_mgr_alloc();

    /* probe manager */
    struct upipe_mgr *probe_uref_mgr = upipe_probe_uref_mgr_alloc();

    /* row_join manager */
    struct upipe_mgr *row_join_mgr = upipe_row_join_mgr_alloc();

    /* pic flow definition */
    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow);
    ubase_assert(uref_pic_flow_add_plane(flow, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow, 2, 2, 1, "v8"));
    ubase_assert(uref_pic_flow_set_hsize(flow, WIDTH));
    ubase_assert(uref_pic_flow_set_vsize(flow, CHUNK_HEIGHT));
    ubase_assert(uref_pic_flow_set_fps(flow, (struct urational){ .num = 25, .den = 1 }));

    /* blksrc pipe */
    blksrc = upipe_flow_alloc(blksrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "blksrc"),
            flow);
    assert(blksrc);

    /* modify flow_def */
    struct upipe *upipe = upipe_void_chain_output(blksrc, setflowdef_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "modify flow_def"));
    assert(upipe);

    struct uref *flow_dup = uref_dup(flow);
    assert(flow_dup);
    ubase_assert(uref_pic_flow_set_vsize(flow_dup, HEIGHT));
    ubase_assert(upipe_setflowdef_set_dict(upipe, flow_dup));
    uref_free(flow);
    uref_free(flow_dup);

    /* set vposition */
    upipe = upipe_void_chain_output(upipe, probe_uref_mgr,
            uprobe_pfx_alloc(uprobe_use(&uprobe_modify_blksrc_urefs),
                UPROBE_LOG_LEVEL, "set vposition"));
    assert(upipe);

    /* row_join pipe */
    upipe = upipe_void_chain_output(upipe, row_join_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "row_join"));
    assert(upipe);

    /* test pipe */
    upipe = upipe_void_chain_output(upipe, &test_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "row_join_test"));
    assert(upipe);

    /* launch test */
    upump_mgr_run(upump_mgr, NULL);

    /* release pipes */
    test_free(upipe);

    /* clean everything */
    upipe_mgr_release(blksrc_mgr);
    upipe_mgr_release(setflowdef_mgr);
    upipe_mgr_release(probe_uref_mgr);
    upipe_mgr_release(row_join_mgr);
    uref_mgr_release(uref_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    uprobe_clean(&uprobe_modify_blksrc_urefs);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);
    uclock_release(uclock);

    return 0;
}

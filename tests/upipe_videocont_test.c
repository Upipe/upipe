/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uclock.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf_pic_mem.h>
#include <upipe-modules/upipe_videocont.h>
#include <upipe-modules/upipe_null.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     0
#define ITERATIONS          5
#define INPUT_NUM           7
#define TOLERANCE           UCLOCK_FREQ / 1000
#define UPROBE_LOG_LEVEL UPROBE_LOG_VERBOSE

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    int i, j;
    struct upipe *subpipe[INPUT_NUM];

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* Y only */
    struct ubuf_mgr *pic_mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH,
                        UBUF_POOL_DEPTH, umem_mgr, 1, 0, 0, 0, 0, 0, 0);
    assert(pic_mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(pic_mgr, "y8", 1, 1, 1));

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);

    /* build videocont pipe */
    struct upipe_mgr *upipe_videocont_mgr = upipe_videocont_mgr_alloc();
    assert(upipe_videocont_mgr);
    struct upipe *videocont = upipe_void_alloc(upipe_videocont_mgr,
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "videocont"));
    assert(videocont);

    const char *input_name;
    ubase_assert(upipe_videocont_get_input(videocont, &input_name));
    assert(input_name == NULL);

    ubase_assert(upipe_videocont_set_input(videocont, "bar3"));
    ubase_assert(upipe_videocont_set_tolerance(videocont, TOLERANCE));

    ubase_assert(upipe_videocont_get_input(videocont, &input_name));
    assert(input_name != NULL);
    
    ubase_assert(upipe_videocont_get_current_input(videocont, &input_name));
    assert(input_name == NULL);

    struct uref *flow = uref_pic_flow_alloc_def(uref_mgr, 1);
    ubase_assert(upipe_set_flow_def(videocont, flow));
    uref_free(flow);

    struct upipe *null = upipe_void_alloc_output(videocont, upipe_null_mgr_alloc(),
        uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "null"));
    assert(null);
    upipe_release(null);
    upipe_null_dump_dict(null, true);

    /* input subpipes */
    for (i=0; i < INPUT_NUM; i++) {
        subpipe[i] = upipe_void_alloc_sub(videocont,
            uprobe_pfx_alloc_va(uprobe_use(logger),
                                UPROBE_LOG_LEVEL, "sub%d", i));
        assert(subpipe[i]);
        flow = uref_pic_flow_alloc_def(uref_mgr, 1);
        char name[20];
        snprintf(name, sizeof(name), "bar%d", i);
        uref_flow_set_name(flow, name);
        ubase_assert(upipe_set_flow_def(subpipe[i], flow));
        uref_free(flow);
    }

    input_name = NULL;
    ubase_assert(upipe_videocont_get_current_input(videocont, &input_name));
    assert(input_name != NULL);

    ubase_assert(upipe_videocont_set_input(videocont, "bar2"));
    ubase_assert(upipe_videocont_sub_set_input(subpipe[1]));

    input_name = NULL;
    ubase_assert(upipe_videocont_get_current_input(videocont, &input_name));
    assert(input_name != NULL);

    /* input urefs */
    for (j=0; j < INPUT_NUM; j++) {
        for (i=0; i < 2 * ITERATIONS + j; i++) {
            struct uref *uref = uref_pic_alloc(uref_mgr, pic_mgr, 42, 42);
            uref_clock_set_pts_sys(uref, UCLOCK_FREQ - TOLERANCE / 2
                                         + 5 * i * TOLERANCE);
            upipe_input(subpipe[j], uref, NULL);
        }
    }

    /* now send reference urefs */
    for (i=0; i < ITERATIONS; i++) {
        struct uref *uref = uref_alloc(uref_mgr);
        uref_clock_set_pts_sys(uref, UCLOCK_FREQ + i * TOLERANCE * 10);
        upipe_input(videocont, uref, NULL);
    }

    for (i=0; i < INPUT_NUM; i++) {
        upipe_release(subpipe[i]);
    }

    /* release pipe */
    upipe_release(videocont);

    /* release managers */
    upipe_mgr_release(upipe_videocont_mgr); // no-op
    ubuf_mgr_release(pic_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

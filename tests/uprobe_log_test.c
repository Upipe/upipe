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
 * @short unit tests for uprobe log implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_log.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

static struct upipe test_pipe;

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(mgr != NULL);

    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(NULL, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);
    struct uprobe *uprobe = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_DEBUG);
    assert(uprobe != NULL);
    test_pipe.uprobe = uprobe;

    /* unmask all events */
    uprobe_log_unmask_event(uprobe, UPROBE_CLOCK_REF);
    uprobe_log_unmask_event(uprobe, UPROBE_CLOCK_TS);
    uprobe_log_unmask_unknown_events(uprobe);

    upipe_throw_ready(&test_pipe);
    upipe_throw_fatal(&test_pipe, UPROBE_ERR_ALLOC);
    upipe_throw_error(&test_pipe, UPROBE_ERR_INVALID);
    upipe_throw_source_end(&test_pipe);
    upipe_throw_sink_end(&test_pipe);
    upipe_throw_need_uref_mgr(&test_pipe);
    upipe_throw_need_upump_mgr(&test_pipe);

    struct uref *uref = uref_block_flow_alloc_def(mgr, "test.");
    upipe_throw_need_ubuf_mgr(&test_pipe, uref);
    upipe_throw_new_flow_def(&test_pipe, uref);
    uref_free(uref);

    upipe_split_throw_update(&test_pipe);

    upipe_throw_sync_acquired(&test_pipe);
    upipe_throw_sync_lost(&test_pipe);

    uref = uref_alloc(mgr);
    upipe_throw_clock_ref(&test_pipe, uref, 42, 0);
    upipe_throw_clock_ref(&test_pipe, uref, 43, 1);
    uref_clock_set_pts_orig(uref, 42);
    uref_clock_set_dts_orig(uref, 12);
    upipe_throw_clock_ts(&test_pipe, uref);
    uref_free(uref);
    upipe_throw_dead(&test_pipe);

    uprobe_log_free(uprobe);
    uprobe_stdio_free(uprobe_stdio);

    uref_mgr_release(mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

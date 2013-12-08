/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short unit tests for uprobe_dejitter implementation
 */

#undef NDEBUG

#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_dejitter.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0

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
            break;
    }
    return true;
}

int main(int argc, char **argv)
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
                                               UPROBE_LOG_DEBUG);
    assert(logger != NULL);

    struct uprobe *uprobe_dejitter = uprobe_dejitter_alloc(logger, 10);
    assert(uprobe_dejitter != NULL);

    struct upipe test_pipe;
    test_pipe.uprobe = uprobe_dejitter;
    struct upipe *upipe = &test_pipe;

    uint64_t systime = UINT32_MAX;
    uint64_t clock = 0;
    uint64_t pts;
    struct uref *uref = uref_alloc(uref_mgr);
    assert(uref != NULL);

    uref_clock_set_cr_sys(uref, systime);
    upipe_throw_clock_ref(upipe, uref, clock, 1);

    uref_clock_set_pts_prog(uref, clock);
    upipe_throw_clock_ts(upipe, uref);
    assert(uref_clock_get_pts_sys(uref, &pts));
    assert(pts == systime);

    systime += 8000;
    clock += 10000;
    uref_clock_set_cr_sys(uref, systime);
    upipe_throw_clock_ref(upipe, uref, clock, 0);

    uref_clock_set_pts_prog(uref, clock);
    upipe_throw_clock_ts(upipe, uref);
    assert(uref_clock_get_pts_sys(uref, &pts));
    assert(pts - 9500 == systime - 8000);

    systime += 12000;
    clock += 10000;
    uref_clock_set_cr_sys(uref, systime);
    upipe_throw_clock_ref(upipe, uref, clock, 0);

    uref_clock_set_pts_prog(uref, clock);
    upipe_throw_clock_ts(upipe, uref);
    assert(uref_clock_get_pts_sys(uref, &pts));
    assert(pts - 19888 == systime - 20000);

    systime += UINT32_MAX;
    clock += 10000;
    uref_clock_set_cr_sys(uref, systime);
    upipe_throw_clock_ref(upipe, uref, clock, 1);

    uref_clock_set_pts_prog(uref, clock);
    upipe_throw_clock_ts(upipe, uref);
    assert(uref_clock_get_pts_sys(uref, &pts));
    assert(pts - 416 == systime);
    /* 416 = due to previous deviation */

    uref_free(uref);
    uprobe_dejitter_free(uprobe_dejitter);
    uprobe_stdio_free(logger);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

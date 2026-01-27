/*
 * Copyright (C) 2013-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for uprobe_dejitter implementation
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_dejitter.h"
#include "upipe/upipe.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_std.h"

#include <stdio.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0

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
            break;
    }
    return UBASE_ERR_NONE;
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
                                               UPROBE_LOG_VERBOSE);
    assert(logger != NULL);

    struct uprobe *uprobe_dejitter = uprobe_dejitter_alloc(uprobe_use(logger),
                                                           true, 1);
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
    ubase_assert(uref_clock_get_pts_sys(uref, &pts));
    assert(pts == systime + 3);

    systime += 8000;
    clock += 10000;
    uref_clock_set_cr_sys(uref, systime);
    upipe_throw_clock_ref(upipe, uref, clock, 0);

    uref_clock_set_pts_prog(uref, clock);
    upipe_throw_clock_ts(upipe, uref);
    ubase_assert(uref_clock_get_pts_sys(uref, &pts));
    assert(pts == systime + 2003);

    uref_free(uref);
    uprobe_release(uprobe_dejitter);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

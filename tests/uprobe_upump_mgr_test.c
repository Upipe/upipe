/*
 * Copyright (C) 2013-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for uprobe_upump_mgr implementation
 */

#undef NDEBUG

#include "upipe/uprobe_upump_mgr.h"
#include "upipe/upipe.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1

static struct upump_mgr *upump_mgr;

/** helper phony pipe to test uprobe_upump_mgr */
static struct upipe *uprobe_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    struct upump_mgr *m;
    upipe_throw_need_upump_mgr(upipe, &m);
    assert(m == upump_mgr);
    upump_mgr_release(m);
    return upipe;
}

/** helper phony pipe to test uprobe_upump_mgr */
static void uprobe_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_upump_mgr */
static struct upipe_mgr uprobe_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = uprobe_test_alloc,
    .upipe_input = NULL,
    .upipe_control = NULL
};

int main(int argc, char **argv)
{
    upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct uprobe *uprobe = uprobe_upump_mgr_alloc(NULL, upump_mgr);
    assert(uprobe != NULL);
    upump_mgr_release(upump_mgr);

    struct upipe *upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe);
    uprobe_test_free(upipe);

    return 0;
}

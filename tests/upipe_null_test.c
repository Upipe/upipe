/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * SPDX-License-Identifier: MIT
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_null.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define ITERATIONS 50
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
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
    int i;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_DEBUG);
    assert(logger != NULL);

    /* build null pipe */
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    assert(upipe_null_mgr);
    struct upipe *nullpipe = upipe_void_alloc(upipe_null_mgr,
               uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "null"));
    assert(nullpipe);

    /* Now send uref */
    for (i=0; i < ITERATIONS; i++) {
        upipe_input(nullpipe, uref_alloc(uref_mgr), NULL);
    }
    /* Same with dump */
    upipe_null_dump_dict(nullpipe, true);
    for (i=0; i < ITERATIONS; i++) {
        upipe_input(nullpipe, uref_alloc_control(uref_mgr), NULL);
    }

    /* release pipe */
    upipe_release(nullpipe);

    /* release managers */
    upipe_mgr_release(upipe_null_mgr); // no-op
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

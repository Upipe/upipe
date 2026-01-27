/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for standard uref manager
 */

#undef NDEBUG

#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"

#include <stdio.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(mgr != NULL);

    struct uref *uref1 = uref_alloc(mgr);
    assert(uref1 != NULL);

    struct uref *uref2 = uref_dup(uref1);
    assert(uref2 != NULL);
    assert(uref2 != uref1);
    uref_free(uref1);
    uref_free(uref2);

    uref2 = uref_alloc(mgr);
    assert(uref2 != NULL);
    assert(uref1 == uref2); // because the pool is 1 packet deep
    uref_free(uref2);

    uref1 = uref_alloc_control(mgr);
    assert(uref1 != NULL);
    uref_free(uref1);

    uref_mgr_release(mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

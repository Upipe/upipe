/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for umem alloc manager
 */

#undef NDEBUG

#include "upipe/umem.h"
#include "upipe/umem_alloc.h"

#include <stdint.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct umem_mgr *mgr = umem_alloc_mgr_alloc();
    assert(mgr != NULL);

    struct umem umem;
    assert(umem_alloc(mgr, &umem, 42));
    uint8_t *p = umem_buffer(&umem);
    assert(p != NULL);
    memset(p, 0x42, 42);

    assert(umem_realloc(&umem, 43));
    p = umem_buffer(&umem);
    assert(p != NULL);
    assert(p[0] == 0x42);
    assert(p[41] == 0x42);
    p[42] = 0x43;

    assert(umem_realloc(&umem, 8192));
    p = umem_buffer(&umem);
    assert(p != NULL);
    assert(p[0] == 0x42);
    assert(p[41] == 0x42);
    assert(p[42] == 0x43);
    memset(p + 43, 0x44, 8192 - 43);

    assert(umem_realloc( &umem, 64));
    p = umem_buffer(&umem);
    assert(p != NULL);
    assert(p[0] == 0x42);
    assert(p[41] == 0x42);
    assert(p[42] == 0x43);
    assert(p[43] == 0x44);
    assert(p[63] == 0x44);
    umem_free(&umem);

    umem_mgr_release(mgr);
    return 0;
}

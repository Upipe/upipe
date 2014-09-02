/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
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
 * @short unit tests for umem pool manager
 */

#undef NDEBUG

#include <upipe/umem.h>
#include <upipe/umem_pool.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct umem_mgr *mgr = umem_pool_mgr_alloc_simple(32);
    assert(mgr != NULL);

    struct umem umem;
    assert(umem_alloc(mgr, &umem, 42));
    uint8_t *p = umem_buffer(&umem);
    assert(p != NULL);
    memset(p, 0x42, 42);
    printf("Passed 1\n");

    assert(umem_realloc(&umem, 43));
    p = umem_buffer(&umem);
    assert(p != NULL);
    assert(p[0] == 0x42);
    assert(p[41] == 0x42);
    p[42] = 0x43;
    printf("Passed 2\n");

    assert(umem_realloc(&umem, 8192));
    p = umem_buffer(&umem);
    assert(p != NULL);
    assert(p[0] == 0x42);
    assert(p[41] == 0x42);
    assert(p[42] == 0x43);
    memset(p + 43, 0x44, 8192 - 43);
    printf("Passed 3\n");

    assert(umem_realloc(&umem, 64));
    p = umem_buffer(&umem);
    assert(p != NULL);
    assert(p[0] == 0x42);
    assert(p[41] == 0x42);
    assert(p[42] == 0x43);
    assert(p[43] == 0x44);
    assert(p[63] == 0x44);
    umem_free(&umem);
    printf("Passed 4\n");

    assert(umem_alloc(mgr, &umem, 8192));
    assert(umem_buffer(&umem) == p);
    umem_free(&umem);
    printf("Passed 5\n");

    assert(umem_alloc(mgr, &umem, 128));
    assert(umem_buffer(&umem) != p);
    umem_free(&umem);
    printf("Passed 6\n");

    umem_mgr_release(mgr);
    return 0;
}

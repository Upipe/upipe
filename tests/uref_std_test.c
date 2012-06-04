/*****************************************************************************
 * uref_std_test.c: unit tests for uref manager
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#include <stdio.h>
#include <string.h>

#undef NDEBUG
#include <assert.h>

#include <upipe/uref.h>
#include <upipe/uref_std.h>

#define UREF_POOL_DEPTH 1

int main(int argc, char **argv)
{
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, -1, -1);
    assert(mgr != NULL);

    struct uref *uref1 = uref_alloc(mgr);
    assert(uref1 != NULL);

    struct uref *uref2 = uref_dup(mgr, uref1);
    assert(uref1 != NULL);
    assert(uref2 != uref1);
    uref_release(uref1);
    uref_release(uref2);

    uref2 = uref_alloc(mgr);
    assert(uref1 == uref2); // because the pool is 1 packet deep
    uref_release(uref2);

    uref1 = uref_alloc_control(mgr);
    assert(uref1 != NULL);
    assert(uref2 != uref1);

    uref_mgr_release(mgr);
    assert(urefcount_single(&mgr->refcount));
    uref_release(uref1);
    return 0;
}

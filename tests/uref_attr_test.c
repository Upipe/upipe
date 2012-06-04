/*****************************************************************************
 * uref_attr_test.c: unit tests for uref attributes
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
#include <stdbool.h>
#include <stdint.h>

#undef NDEBUG
#include <assert.h>

#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/uref_std.h>

#define UREF_POOL_DEPTH 1

#define SALUTATION "Hello everyone, this is just some padding to make the structure bigger, if you don't mind."

int main(int argc, char **argv)
{
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, -1, -1);
    assert(mgr != NULL);

    struct uref *uref1 = uref_alloc(mgr);
    assert(uref1 != NULL);

    uint8_t opaque[27];
    memset(opaque, 0xaa, sizeof(opaque));
    assert(uref_attr_set_opaque(&uref1, opaque, sizeof(opaque), "x.opaque"));
    assert(uref_attr_set_string(&uref1, "pouet", "f.def"));
    assert(uref_attr_set_void(&uref1, NULL, "f.error"));
    assert(uref_attr_set_bool(&uref1, true, "x.truc"));
    assert(uref_attr_set_unsigned(&uref1, UINT64_MAX, "k.pts"));
    assert(uref_attr_set_int_va(&uref1, INT64_MAX, "x.date[%d]", 400));
    assert(uref_attr_set_float(&uref1, 1.0, "x.version"));
    assert(uref_attr_set_string(&uref1, SALUTATION, "x.salutation"));
    struct urational rational = { num: 64, den: 45 };
    assert(uref_attr_set_rational_va(&uref1, rational, "x.ar[%d]", 0));

    const uint8_t *opaque2;
    size_t size;
    assert(uref_attr_get_opaque(uref1, &opaque2, &size, "x.opaque"));
    assert(size == sizeof(opaque));
    assert(!memcmp(opaque, opaque2, sizeof(opaque)));
    const char *string;
    assert(uref_attr_get_string(uref1, &string, "f.def"));
    assert(!strcmp(string, "pouet"));
    assert(!uref_attr_get_void(uref1, NULL, "f.eof"));
    assert(uref_attr_get_void(uref1, NULL, "f.error"));

    assert(uref_attr_delete_void(uref1, "f.error"));
    assert(uref_attr_delete_string(uref1, "f.def"));
    assert(!uref_attr_delete_void(uref1, "x.truc"));
    assert(!uref_attr_delete_bool(uref1, "k.pts"));

    bool b;
    assert(uref_attr_get_bool(uref1, &b, "x.truc"));
    assert(b);
    uint64_t u;
    assert(uref_attr_get_unsigned(uref1, &u, "k.pts"));
    assert(u == UINT64_MAX);
    int64_t d;
    assert(uref_attr_get_int_va(uref1, &d, "x.date[%d]", 400));
    assert(d == INT64_MAX);
    double f;
    assert(uref_attr_get_float(uref1, &f, "x.version"));
    assert(f == 1.0);
    assert(uref_attr_get_string(uref1, &string, "x.salutation"));
    assert(!strcmp(string, SALUTATION));
    struct urational r;
    assert(uref_attr_get_rational_va(uref1, &r, "x.ar[%d]", 0));
    assert(r.num == 64 && r.den == 45);

    uref_mgr_release(mgr);
    uref_release(uref1);
    return 0;
}

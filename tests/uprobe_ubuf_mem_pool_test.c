/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for uprobe_ubuf_mem_pool implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_ubuf_mem_pool.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/urequest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0

static struct uref *flow_def;
static void (*test_mgr)(struct ubuf_mgr *);
static struct ubuf_mgr *previous_ubuf_mgr = NULL;

static void test_I420(struct ubuf_mgr *mgr)
{
    assert(ubuf_pic_alloc(mgr, 31, 32) == NULL);
    assert(ubuf_pic_alloc(mgr, 32, 31) == NULL);

    struct ubuf *ubuf = ubuf_pic_alloc(mgr, 32, 32);
    assert(ubuf != NULL);

    size_t hsize, vsize;
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, &hsize, &vsize, &macropixel));
    assert(hsize == 32);
    assert(vsize == 32);
    assert(macropixel == 1);

    const char *chroma = NULL;
    unsigned int nb_planes = 0;
    while (ubase_check(ubuf_pic_plane_iterate(ubuf, &chroma)) &&
           chroma != NULL) {
        nb_planes++;
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                         &macropixel_size));
        assert(!(stride % 16)); //align
        if (!strcmp(chroma, "y8")) {
            assert(stride >= 32 + 4 + 8);
            assert(hsub == 1);
            assert(vsub == 1);
            assert(macropixel_size == 1);
        } else if (!strcmp(chroma, "u8") || !strcmp(chroma, "v8")) {
            assert(stride >= 16 + 4 / 2 + 8 / 2);
            assert(hsub == 2);
            assert(vsub == 2);
            assert(macropixel_size == 1);
        } else
            assert(0);
    }
    assert(nb_planes == 3);

    const uint8_t *r;
    ubase_assert(ubuf_pic_plane_read(ubuf, "y8", 0, 0, -1, -1, &r));
    assert(!((uintptr_t)r % 16));
    ubase_assert(ubuf_pic_plane_unmap(ubuf, "y8", 0, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf, "u8", 0, 0, -1, -1, &r));
    assert(!((uintptr_t)r % 16));
    ubase_assert(ubuf_pic_plane_unmap(ubuf, "u8", 0, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf, "v8", 0, 0, -1, -1, &r));
    assert(!((uintptr_t)r % 16));
    ubase_assert(ubuf_pic_plane_unmap(ubuf, "v8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_resize(ubuf, -2, -4, 36, 40));
    ubase_nassert(ubuf_pic_resize(ubuf, -1, 0, -1, -1));
    ubase_nassert(ubuf_pic_resize(ubuf, 0, -1, -1, -1));
    ubase_nassert(ubuf_pic_resize(ubuf, 0, 0, 37, -1));
    ubase_nassert(ubuf_pic_resize(ubuf, 0, 0, -1, 37));

    ubuf_free(ubuf);
}

static void test_YUYV(struct ubuf_mgr *mgr)
{
    assert(ubuf_pic_alloc(mgr, 31, 32) == NULL);

    struct ubuf *ubuf = ubuf_pic_alloc(mgr, 32, 32);
    assert(ubuf != NULL);

    size_t hsize, vsize;
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, &hsize, &vsize, &macropixel));
    assert(hsize == 32);
    assert(vsize == 32);
    assert(macropixel == 2);

    const char *chroma = NULL;
    unsigned int nb_planes = 0;
    while (ubase_check(ubuf_pic_plane_iterate(ubuf, &chroma)) &&
           chroma != NULL) {
        nb_planes++;
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                          &macropixel_size));
        assert(!(stride % 16)); //align
        assert(!strcmp(chroma, "y8u8y8v8"));
        assert(stride >= (32 + 2 + 4) * 4 / 2);
        assert(hsub == 1);
        assert(vsub == 1);
        assert(macropixel_size == 4);
    }
    assert(nb_planes == 1);

    const uint8_t *r;
    ubase_assert(ubuf_pic_plane_read(ubuf, "y8u8y8v8", 0, 0, -1, -1, &r));
    assert(!((uintptr_t)r % 16));
    ubase_assert(ubuf_pic_plane_unmap(ubuf, "y8u8y8v8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_resize(ubuf, -2, -3, 38, 39));
    ubase_nassert(ubuf_pic_resize(ubuf, -2, 0, -1, -1));
    ubase_nassert(ubuf_pic_resize(ubuf, 0, -1, -1, -1));
    ubase_nassert(ubuf_pic_resize(ubuf, 0, 0, 39, -1));
    ubase_nassert(ubuf_pic_resize(ubuf, 0, 0, -1, 40));

    ubuf_free(ubuf);
}

/** helper phony pipe to test uprobe_ubuf_mem_pool */
static int uprobe_test_provide_ubuf_mgr(struct urequest *urequest, va_list args)
{
    struct ubuf_mgr *m = va_arg(args, struct ubuf_mgr *);
    assert(m != NULL);
    if (previous_ubuf_mgr != NULL)
        assert(m == previous_ubuf_mgr);
    previous_ubuf_mgr = m;
    test_mgr(m);
    ubuf_mgr_release(m);
    struct uref *flow_def = va_arg(args, struct uref *);
    uref_free(flow_def);
    return UBASE_ERR_NONE;
}

/** helper phony pipe to test uprobe_ubuf_mem_pool */
static struct upipe *uprobe_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    struct urequest request;
    urequest_init_ubuf_mgr(&request, flow_def, uprobe_test_provide_ubuf_mgr,
                           NULL);
    return upipe;
}

/** helper phony pipe to test uprobe_ubuf_mem_pool */
static void uprobe_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_ubuf_mem_pool */
static struct upipe_mgr uprobe_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = uprobe_test_alloc,
    .upipe_input = NULL,
    .upipe_control = NULL
};

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

    struct uprobe *uprobe = uprobe_ubuf_mem_pool_alloc(NULL, umem_mgr,
            UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe != NULL);

    /* planar I420 */
    test_mgr = test_I420;
    flow_def = uref_pic_flow_alloc_def(uref_mgr, 1);
    assert(flow_def != NULL);
    ubase_assert(uref_pic_flow_set_hmprepend(flow_def, 2));
    ubase_assert(uref_pic_flow_set_hmappend(flow_def, 2));
    ubase_assert(uref_pic_flow_set_vprepend(flow_def, 4));
    ubase_assert(uref_pic_flow_set_vappend(flow_def, 4));
    ubase_assert(uref_pic_flow_set_align(flow_def, 16));
    ubase_assert(uref_pic_flow_set_align_hmoffset(flow_def, 0));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 1, "y8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "u8"));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 2, 2, 1, "v8"));

    struct upipe *upipe;
    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    uprobe_test_free(upipe);

    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    uprobe_test_free(upipe);
    uref_free(flow_def);
    previous_ubuf_mgr = NULL;

    /* packed YUYV */
    test_mgr = test_YUYV;
    flow_def = uref_pic_flow_alloc_def(uref_mgr, 2);
    ubase_assert(uref_pic_flow_set_hmprepend(flow_def, 1));
    ubase_assert(uref_pic_flow_set_hmappend(flow_def, 2));
    ubase_assert(uref_pic_flow_set_vprepend(flow_def, 3));
    ubase_assert(uref_pic_flow_set_vappend(flow_def, 4));
    ubase_assert(uref_pic_flow_set_align(flow_def, 16));
    ubase_assert(uref_pic_flow_set_align_hmoffset(flow_def, 0));
    ubase_assert(uref_pic_flow_add_plane(flow_def, 1, 1, 4, "y8u8y8v8"));

    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    uprobe_test_free(upipe);

    upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe_use(uprobe));
    uprobe_test_free(upipe);
    uref_free(flow_def);
    previous_ubuf_mgr = NULL;

    uprobe_release(uprobe);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

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
 * @short unit tests for ubuf manager for picture formats
 */

#undef NDEBUG

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_mem.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UBUF_POOL_DEPTH     1
#define UBUF_PREPEND        2
#define UBUF_APPEND         2
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0

static void fill_in(struct ubuf *ubuf)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, &hsize, &vsize, &macropixel));

    const char *chroma = NULL;
    while (ubase_check(ubuf_pic_plane_iterate(ubuf, &chroma)) &&
           chroma != NULL) {
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                         &macropixel_size));
        int hoctets = hsize * macropixel_size / hsub / macropixel;
        uint8_t *buffer;
        ubase_assert(ubuf_pic_plane_write(ubuf, chroma, 0, 0, -1, -1, &buffer));

        for (int y = 0; y < vsize / vsub; y++) {
            for (int x = 0; x < hoctets; x++)
                buffer[x] = 1 + (y * hoctets) + x;
            buffer += stride;
        }
        ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1));
    }
}

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);

    struct ubuf_mgr *mgr;
    struct ubuf *ubuf1, *ubuf2;
    const char *chroma;
    size_t hsize, vsize;
    uint8_t macropixel;
    uint8_t *w;
    const uint8_t *r;

    /* planar I420 */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "v8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "u8", 2, 2, 1));

    assert(ubuf_pic_alloc(mgr, 31, 32) == NULL);
    assert(ubuf_pic_alloc(mgr, 32, 31) == NULL);

    ubuf1 = ubuf_pic_alloc(mgr, 32, 32);
    assert(ubuf1 != NULL);

    ubase_assert(ubuf_pic_size(ubuf1, &hsize, &vsize, &macropixel));
    assert(hsize == 32);
    assert(vsize == 32);
    assert(macropixel == 1);

    chroma = NULL;
    unsigned int nb_planes = 0;
    while (ubase_check(ubuf_pic_plane_iterate(ubuf1, &chroma)) &&
           chroma != NULL) {
        nb_planes++;
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        ubase_assert(ubuf_pic_plane_size(ubuf1, chroma, &stride, &hsub, &vsub,
                                         &macropixel_size));
        if (!strcmp(chroma, "y8")) {
            assert(stride >= 32 + UBUF_PREPEND + UBUF_APPEND);
            assert(hsub == 1);
            assert(vsub == 1);
            assert(macropixel_size == 1);
        } else if (!strcmp(chroma, "u8") || !strcmp(chroma, "v8")) {
            assert(stride >= 16 + UBUF_PREPEND / 2 + UBUF_APPEND / 2);
            assert(hsub == 2);
            assert(vsub == 2);
            assert(macropixel_size == 1);
        } else
            assert(0);
    }
    assert(nb_planes == 3);

    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8", 0, 0, -1, -1, &r));
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_clear(ubuf1, 0, 0, -1, -1, 0));
    fill_in(ubuf1);

    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8", 2, 2, 1, 1, &r));
    assert(*r == 1 + 2 * 32 + 2);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8", 2, 2, 1, 1));

    ubuf2 = ubuf_dup(ubuf1);
    assert(ubuf2 != NULL);
    ubase_nassert(ubuf_pic_plane_write(ubuf1, "y8", 0, 0, -1, -1, &w));
    ubuf_free(ubuf2);

    ubase_nassert(ubuf_pic_resize(ubuf1, 1, 0, 31, 32));
    ubase_nassert(ubuf_pic_resize(ubuf1, -1, 0, 33, 32));
    ubase_nassert(ubuf_pic_resize(ubuf1, 0, 1, 32, 31));
    ubase_nassert(ubuf_pic_resize(ubuf1, 0, -1, 32, 33));

    ubase_assert(ubuf_pic_resize(ubuf1, 2, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8", 0, 0, -1, -1, &r));
    assert(r[0] == 3);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8", 0, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "u8", 0, 0, -1, -1, &r));
    assert(r[0] == 2);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "u8", 0, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "v8", 0, 0, -1, -1, &r));
    assert(r[0] == 2);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "v8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_resize(ubuf1, 0, 2, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8", 0, 0, -1, -1, &r));
    assert(r[0] == 2 * 32 + 3);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8", 0, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "u8", 0, 0, -1, -1, &r));
    assert(r[0] == 16 + 2);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "u8", 0, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "v8", 0, 0, -1, -1, &r));
    assert(r[0] == 16 + 2);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "v8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_resize(ubuf1, -4, -2, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8", 2, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8", 2, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "u8", 2, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "u8", 2, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "v8", 2, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "v8", 2, 0, -1, -1));

    ubase_nassert(ubuf_pic_resize(ubuf1, -2, 0, -1, -1));
    ubase_assert(ubuf_pic_replace(mgr, &ubuf1, -2, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8", 4, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8", 4, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "u8", 4, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "u8", 4, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "v8", 4, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "v8", 4, 0, -1, -1));

    ubuf_free(ubuf1);

    ubuf_mgr_release(mgr);

    /* packed YUYV */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 2,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y8u8y8v8", 1, 1, 4));

    assert(ubuf_pic_alloc(mgr, 31, 32) == NULL);

    ubuf1 = ubuf_pic_alloc(mgr, 32, 32);
    assert(ubuf1 != NULL);

    ubase_assert(ubuf_pic_size(ubuf1, &hsize, &vsize, &macropixel));
    assert(hsize == 32);
    assert(vsize == 32);
    assert(macropixel == 2);

    chroma = NULL;
    nb_planes = 0;
    while (ubase_check(ubuf_pic_plane_iterate(ubuf1, &chroma)) &&
           chroma != NULL) {
        nb_planes++;
        size_t stride;
        uint8_t hsub, vsub, macropixel_size;
        ubase_assert(ubuf_pic_plane_size(ubuf1, chroma, &stride, &hsub, &vsub,
                                          &macropixel_size));
        assert(!strcmp(chroma, "y8u8y8v8"));
        assert(stride >= (32 + UBUF_PREPEND + UBUF_APPEND) * 4 / 2);
        assert(hsub == 1);
        assert(vsub == 1);
        assert(macropixel_size == 4);
    }
    assert(nb_planes == 1);

    fill_in(ubuf1);

    ubuf2 = ubuf_dup(ubuf1);
    assert(ubuf2 != NULL);
    ubase_nassert(ubuf_pic_plane_write(ubuf1, "y8u8y8v8", 0, 0, -1, -1, &w));
    ubuf_free(ubuf2);

    ubase_nassert(ubuf_pic_resize(ubuf1, 1, 0, 31, 32));
    ubase_nassert(ubuf_pic_resize(ubuf1, -1, 0, 33, 32));

    ubase_assert(ubuf_pic_resize(ubuf1, 2, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8u8y8v8", 0, 0, -1, -1, &r));
    assert(r[0] == 5);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8u8y8v8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_resize(ubuf1, 0, 2, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8u8y8v8", 0, 0, -1, -1, &r));
    assert(r[0] == 2 * 64 + 5);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8u8y8v8", 0, 0, -1, -1));

    ubase_assert(ubuf_pic_resize(ubuf1, -4, -2, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8u8y8v8", 2, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8u8y8v8", 2, 0, -1, -1));

    ubase_nassert(ubuf_pic_resize(ubuf1, -2, 0, -1, -1));
    ubase_assert(ubuf_pic_replace(mgr, &ubuf1, -2, 0, -1, -1));
    ubase_assert(ubuf_pic_plane_read(ubuf1, "y8u8y8v8", 4, 0, -1, -1, &r));
    assert(r[0] == 1);
    ubase_assert(ubuf_pic_plane_unmap(ubuf1, "y8u8y8v8", 2, 0, -1, -1));

    ubuf_free(ubuf1);

    ubuf_mgr_release(mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

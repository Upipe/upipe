/*****************************************************************************
 * ubuf_pic_test.c: unit tests for ubuf manager for picture formats
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

#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>

#define UBUF_POOL_DEPTH     1
#define UBUF_PREPEND        2
#define UBUF_APPEND         2
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0

static void fill_in(struct ubuf *ubuf, uint8_t plane, size_t hsize, size_t vsize)
{
    uint8_t *buffer = ubuf->planes[plane].buffer;
    for (int y = 0; y < vsize; y++) {
        for (int x = 0; x < hsize; x++)
            buffer[x] = 1 + (y * hsize) + x;
        buffer += ubuf->planes[plane].stride;
    }
}

static void compare(struct ubuf *ubuf1, struct ubuf *ubuf2, uint8_t plane, size_t hsize, size_t vsize)
{
    uint8_t *buffer1 = ubuf1->planes[plane].buffer;
    uint8_t *buffer2 = ubuf2->planes[plane].buffer;
    for (int y = 0; y < vsize; y++) {
        assert(!memcmp(buffer1, buffer2, hsize));
        buffer1 += ubuf1->planes[plane].stride;
        buffer2 += ubuf2->planes[plane].stride;
    }
}

int main(int argc, char **argv)
{
    struct ubuf_mgr *mgr;
    struct ubuf *ubuf1, *ubuf2;

    /* planar I420 */
    mgr = ubuf_pic_mgr_alloc(UBUF_POOL_DEPTH, 1,
                             UBUF_PREPEND, UBUF_APPEND,
                             UBUF_PREPEND, UBUF_APPEND,
                             UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    assert(ubuf_pic_mgr_add_plane(mgr, 1, 1, 1));
    assert(ubuf_pic_mgr_add_plane(mgr, 2, 2, 1));
    assert(ubuf_pic_mgr_add_plane(mgr, 2, 2, 1));

    assert(ubuf_pic_alloc(mgr, 31, 32) == NULL);
    assert(ubuf_pic_alloc(mgr, 32, 31) == NULL);

    ubuf1 = ubuf_pic_alloc(mgr, 32, 32);
    assert(ubuf1 != NULL);
    assert(urefcount_single(&ubuf1->refcount));

    fill_in(ubuf1, 0, 32, 32);
    fill_in(ubuf1, 1, 16, 16);
    fill_in(ubuf1, 2, 16, 16);

    ubuf2 = ubuf1;
    ubuf_use(ubuf2);
    assert(ubuf_writable(mgr, &ubuf2));
    assert(ubuf1 != ubuf2);
    compare(ubuf1, ubuf2, 0, 32, 32);
    compare(ubuf1, ubuf2, 1, 16, 16);
    compare(ubuf1, ubuf2, 2, 16, 16);
    ubuf_release(ubuf2);

    assert(!ubuf_pic_resize(mgr, &ubuf1, 31, 32, 1, 0));
    assert(!ubuf_pic_resize(mgr, &ubuf1, 33, 32, -1, 0));
    assert(!ubuf_pic_resize(mgr, &ubuf1, 32, 31, 0, 1));
    assert(!ubuf_pic_resize(mgr, &ubuf1, 32, 33, 0, -1));

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, 2, 0));
    assert(ubuf1->planes[0].buffer[0] == 3);
    assert(ubuf1->planes[1].buffer[0] == 2);
    assert(ubuf1->planes[2].buffer[0] == 2);

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, 0, 2));
    assert(ubuf1->planes[0].buffer[0] == 2 * 32 + 3);
    assert(ubuf1->planes[1].buffer[0] == 16 + 2);
    assert(ubuf1->planes[2].buffer[0] == 16 + 2);

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, -4, -2));
    assert(ubuf1->planes[0].buffer[2] == 1);
    assert(ubuf1->planes[1].buffer[1] == 1);
    assert(ubuf1->planes[2].buffer[1] == 1);

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, -2, 0));
    assert(ubuf1->planes[0].buffer[4] == 1);
    assert(ubuf1->planes[1].buffer[2] == 1);
    assert(ubuf1->planes[2].buffer[2] == 1);

    ubuf_release(ubuf1);

    assert(urefcount_single(&mgr->refcount));
    ubuf_mgr_release(mgr);

    /* packed YUYV */
    mgr = ubuf_pic_mgr_alloc(UBUF_POOL_DEPTH, 2,
                             UBUF_PREPEND, UBUF_APPEND,
                             UBUF_PREPEND, UBUF_APPEND,
                             UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    assert(ubuf_pic_mgr_add_plane(mgr, 1, 1, 4));

    assert(ubuf_pic_alloc(mgr, 31, 32) == NULL);

    ubuf1 = ubuf_pic_alloc(mgr, 32, 32);
    assert(ubuf1 != NULL);
    assert(urefcount_single(&ubuf1->refcount));

    fill_in(ubuf1, 0, 64, 32);

    ubuf2 = ubuf1;
    ubuf_use(ubuf2);
    assert(ubuf_writable(mgr, &ubuf2));
    assert(ubuf1 != ubuf2);
    compare(ubuf1, ubuf2, 0, 64, 32);
    ubuf_release(ubuf2);

    assert(!ubuf_pic_resize(mgr, &ubuf1, 31, 32, 1, 0));
    assert(!ubuf_pic_resize(mgr, &ubuf1, 33, 32, -1, 0));

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, 2, 0));
    assert(ubuf1->planes[0].buffer[0] == 5);

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, 0, 2));
    assert(ubuf1->planes[0].buffer[0] == 2 * 64 + 5);

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, -4, -2));
    assert(ubuf1->planes[0].buffer[4] == 1);

    assert(ubuf_pic_resize(mgr, &ubuf1, -1, -1, -2, 0));
    assert(ubuf1->planes[0].buffer[8] == 1);

    ubuf_release(ubuf1);

    assert(urefcount_single(&mgr->refcount));
    ubuf_mgr_release(mgr);
    return 0;
}

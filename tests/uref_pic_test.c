/*****************************************************************************
 * ubuf_pic_test.c: unit tests for uref semantics for picture formats
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
#include <upipe/uref.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_std.h>

#define UREF_POOL_DEPTH     1
#define UBUF_POOL_DEPTH     1
#define UBUF_PREPEND        2
#define UBUF_APPEND         2
#define UBUF_ALIGN          16
#define UBUF_ALIGN_HOFFSET  0

static void fill_in(struct uref *uref, struct uref *pic_flow,
                    const char *chroma, uint8_t hsub, uint8_t vsub,
                    uint8_t macropixel_size)
{
    size_t hsize, vsize, stride;
    uint8_t *buffer = uref_pic_chroma(uref, pic_flow, chroma, &stride);
    assert(buffer != NULL);
    uref_pic_size(uref, &hsize, &vsize);
    hsize /= hsub;
    hsize *= macropixel_size;
    vsize /= vsub;
    for (int y = 0; y < vsize; y++) {
        for (int x = 0; x < hsize; x++)
            buffer[x] = 1 + (y * hsize) + x;
        buffer += stride;
    }
}

int main(int argc, char **argv)
{
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, -1, -1);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr;
    struct uref *pic_flow, *uref1, *uref2;
    size_t hsize, vsize, stride;
    uint8_t *buffer;

    /* planar I420 */
    ubuf_mgr = ubuf_pic_mgr_alloc(UBUF_POOL_DEPTH, 1,
                                  UBUF_PREPEND, UBUF_APPEND,
                                  UBUF_PREPEND, UBUF_APPEND,
                                  UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(ubuf_mgr != NULL);
    assert(ubuf_pic_mgr_add_plane(ubuf_mgr, 1, 1, 1));
    assert(ubuf_pic_mgr_add_plane(ubuf_mgr, 2, 2, 1));
    assert(ubuf_pic_mgr_add_plane(ubuf_mgr, 2, 2, 1));

    pic_flow = uref_pic_flow_alloc_definition(uref_mgr, 1);
    assert(pic_flow != NULL);
    assert(uref_pic_flow_add_plane(&pic_flow, 1, 1, 1, "y8"));
    assert(uref_pic_flow_add_plane(&pic_flow, 2, 2, 1, "u8"));
    assert(uref_pic_flow_add_plane(&pic_flow, 2, 2, 1, "v8"));

    assert(uref_pic_alloc(uref_mgr, ubuf_mgr, 31, 32) == NULL);
    assert(uref_pic_alloc(uref_mgr, ubuf_mgr, 32, 31) == NULL);

    uref1 = uref_pic_alloc(uref_mgr, ubuf_mgr, 32, 32);
    assert(uref1 != NULL);
    assert(uref1->ubuf != NULL);

    fill_in(uref1, pic_flow, "y8", 1, 1, 1);
    fill_in(uref1, pic_flow, "u8", 2, 2, 1);
    fill_in(uref1, pic_flow, "v8", 2, 2, 1);

    uref2 = uref_pic_dup(uref_mgr, uref1);

    assert(!uref_pic_resize(&uref1, ubuf_mgr, pic_flow, 31, 32, 1, 0));
    assert(!uref_pic_resize(&uref1, ubuf_mgr, pic_flow, 33, 32, -1, 0));
    assert(!uref_pic_resize(&uref1, ubuf_mgr, pic_flow, 32, 31, 0, 1));
    assert(!uref_pic_resize(&uref1, ubuf_mgr, pic_flow, 32, 33, 0, -1));

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, 2, 0));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 30);
    assert(vsize == 32);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8", &stride);
    assert(buffer[0] == 3);
    buffer = uref_pic_chroma(uref1, pic_flow, "u8", &stride);
    assert(buffer[0] == 2);
    buffer = uref_pic_chroma(uref1, pic_flow, "v8", &stride);
    assert(buffer[0] == 2);

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, 0, 2));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 30);
    assert(vsize == 30);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8", &stride);
    assert(buffer[0] == 2 * 32 + 3);
    buffer = uref_pic_chroma(uref1, pic_flow, "u8", &stride);
    assert(buffer[0] == 16 + 2);
    buffer = uref_pic_chroma(uref1, pic_flow, "v8", &stride);
    assert(buffer[0] == 16 + 2);

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, -2, -2));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 32);
    assert(vsize == 32);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8", &stride);
    assert(buffer[0] == 1);
    buffer = uref_pic_chroma(uref1, pic_flow, "u8", &stride);
    assert(buffer[0] == 1);
    buffer = uref_pic_chroma(uref1, pic_flow, "v8", &stride);
    assert(buffer[0] == 1);

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, -2, 0));
    assert(uref1->ubuf != uref2->ubuf);
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 34);
    assert(vsize == 32);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8", &stride);
    assert(buffer[2] == 1);
    buffer = uref_pic_chroma(uref1, pic_flow, "u8", &stride);
    assert(buffer[1] == 1);
    buffer = uref_pic_chroma(uref1, pic_flow, "v8", &stride);
    assert(buffer[1] == 1);

    uref_release(uref1);
    uref_release(uref2);
    uref_release(pic_flow);

    assert(urefcount_single(&ubuf_mgr->refcount));
    ubuf_mgr_release(ubuf_mgr);

    /* packed YUYV */
    ubuf_mgr = ubuf_pic_mgr_alloc(UBUF_POOL_DEPTH, 2,
                                  UBUF_PREPEND, UBUF_APPEND,
                                  UBUF_PREPEND, UBUF_APPEND,
                                  UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(ubuf_mgr != NULL);
    assert(ubuf_pic_mgr_add_plane(ubuf_mgr, 1, 1, 4));

    pic_flow = uref_pic_flow_alloc_definition(uref_mgr, 2);
    assert(pic_flow != NULL);
    assert(uref_pic_flow_add_plane(&pic_flow, 1, 1, 4, "y8u8y8v8"));

    assert(uref_pic_alloc(uref_mgr, ubuf_mgr, 31, 32) == NULL);

    uref1 = uref_pic_alloc(uref_mgr, ubuf_mgr, 32, 32);
    assert(uref1 != NULL);
    assert(uref1->ubuf != NULL);

    fill_in(uref1, pic_flow, "y8u8y8v8", 1, 1, 4);

    uref2 = uref_pic_dup(uref_mgr, uref1);

    assert(!uref_pic_resize(&uref1, ubuf_mgr, pic_flow, 31, 32, 1, 0));
    assert(!uref_pic_resize(&uref1, ubuf_mgr, pic_flow, 33, 32, -1, 0));

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, 2, 0));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 30);
    assert(vsize == 32);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8u8y8v8", &stride);
    assert(buffer[0] == 5);

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, 0, 1));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 30);
    assert(vsize == 31);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8u8y8v8", &stride);
    assert(buffer[0] == 128 + 5);

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, -2, -1));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 32);
    assert(vsize == 32);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8u8y8v8", &stride);
    assert(buffer[0] == 1);

    assert(uref_pic_resize(&uref1, ubuf_mgr, pic_flow, -1, -1, -2, 0));
    uref_pic_size(uref1, &hsize, &vsize);
    assert(hsize == 34);
    assert(vsize == 32);
    buffer = uref_pic_chroma(uref1, pic_flow, "y8u8y8v8", &stride);
    assert(buffer[4] == 1);

    uref_release(uref1);
    uref_release(uref2);
    uref_release(pic_flow);

    assert(urefcount_single(&ubuf_mgr->refcount));
    ubuf_mgr_release(ubuf_mgr);

    assert(urefcount_single(&uref_mgr->refcount));
    uref_mgr_release(uref_mgr);
    return 0;
}

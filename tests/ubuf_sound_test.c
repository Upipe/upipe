/*****************************************************************************
 * ubuf_sound_test.c: unit tests for ubuf manager for sound formats
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

#undef NDEBUG

#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UBUF_POOL_DEPTH     1
#define UBUF_CHANNELS       2
#define UBUF_SAMPLE_SIZE    2
#define UBUF_PREPEND        32
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define UBUF_SAMPLES        1024

int main(int argc, char **argv)
{
    struct ubuf_mgr *mgr = ubuf_sound_mgr_alloc(UBUF_POOL_DEPTH, UBUF_CHANNELS,
                                                UBUF_SAMPLE_SIZE, UBUF_PREPEND,
                                                UBUF_ALIGN, UBUF_ALIGN_OFFSET);
    assert(mgr != NULL);
    size_t stride = UBUF_CHANNELS * UBUF_SAMPLE_SIZE;

    struct ubuf *ubuf1 = ubuf_sound_alloc(mgr, UBUF_SAMPLES);
    assert(ubuf1 != NULL);
    assert(urefcount_single(&ubuf1->refcount));
    memset(ubuf1->planes[0].buffer, 0xAA, UBUF_SAMPLES * stride);
    printf("allocation passed\n");

    assert(ubuf_sound_resize(mgr, &ubuf1, UBUF_SAMPLES + UBUF_PREPEND, -UBUF_PREPEND));
    assert(ubuf1->planes[0].buffer[UBUF_PREPEND * stride] == 0xAA);
    assert(ubuf1->planes[0].buffer[(UBUF_SAMPLES + UBUF_PREPEND) * stride - 1] == 0xAA);
    ubuf1->planes[0].buffer[0] = 0xAB;
    printf("simple prepend passed\n");

    assert(ubuf_sound_resize(mgr, &ubuf1, UBUF_SAMPLES + 2 * UBUF_PREPEND, -UBUF_PREPEND));
    assert(ubuf1->planes[0].buffer[2 * UBUF_PREPEND * stride] == 0xAA);
    assert(ubuf1->planes[0].buffer[(UBUF_SAMPLES + 2 * UBUF_PREPEND) * stride - 1] == 0xAA);
    ubuf1->planes[0].buffer[0] = 0xAB;
    printf("memmove prepend passed\n");

    assert(ubuf_sound_resize(mgr, &ubuf1, UBUF_SAMPLES + 2 * UBUF_PREPEND, UBUF_PREPEND));
    assert(ubuf1->planes[0].buffer[UBUF_PREPEND * stride] == 0xAA);
    assert(ubuf1->planes[0].buffer[(UBUF_SAMPLES + UBUF_PREPEND) * stride - 1] == 0xAA);
    ubuf1->planes[0].buffer[(UBUF_SAMPLES + 2 * UBUF_PREPEND) * stride - 1] = 0xAB;
    printf("realloc memmove passed\n");

    assert(ubuf_sound_resize(mgr, &ubuf1, UBUF_SAMPLES, UBUF_PREPEND));
    assert(ubuf1->planes[0].buffer[0] == 0xAA);
    assert(ubuf1->planes[0].buffer[UBUF_SAMPLES * stride - 1] == 0xAA);
    printf("resize passed\n");

    ubuf_release(ubuf1);
    struct ubuf *ubuf2 = ubuf_sound_alloc(mgr, UBUF_SAMPLES + UBUF_PREPEND);
    assert(ubuf2 == ubuf1);
    printf("pool allocation passed\n");

    ubuf1 = ubuf2;
    ubuf_use(ubuf2);
    assert(!urefcount_single(&ubuf1->refcount));

    assert(ubuf_writable(mgr, &ubuf1));
    assert(ubuf1 != ubuf2);
    printf("refcounting passed\n");

    ubuf_release(ubuf1);
    ubuf_release(ubuf2);

    assert(urefcount_single(&mgr->refcount));
    ubuf_mgr_release(mgr);
    printf("release passed\n");
    return 0;
}

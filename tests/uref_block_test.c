/*****************************************************************************
 * uref_block_test.c: unit tests for uref semantics for block formats
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
#include <upipe/ubuf_block.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_std.h>

#define UREF_POOL_DEPTH     1
#define UBUF_POOL_DEPTH     1
#define UBUF_SIZE           188
#define UBUF_PREPEND        32
#define UBUF_APPEND         32
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0

int main(int argc, char **argv)
{
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, -1, -1);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mgr_alloc(UBUF_POOL_DEPTH,
                                                     UBUF_POOL_DEPTH,
                                                     UBUF_SIZE, UBUF_PREPEND,
                                                     UBUF_APPEND, UBUF_ALIGN,
                                                     UBUF_ALIGN_OFFSET);
    assert(ubuf_mgr != NULL);

    struct uref *uref1 = uref_block_alloc(uref_mgr, ubuf_mgr, UBUF_SIZE);
    assert(uref1 != NULL);
    assert(uref1->ubuf != NULL);

    size_t size;
    uint8_t *buffer = uref_block_buffer(uref1, &size);
    assert(buffer == uref1->ubuf->planes[0].buffer);
    assert(size == UBUF_SIZE);

    for (int i = 0; i < size; i++)
        *buffer++ = i;

    struct uref *uref2 = uref_block_dup(uref_mgr, uref1);

    assert(uref_block_resize(&uref1, ubuf_mgr, -1, UBUF_PREPEND));
    buffer = uref_block_buffer(uref1, &size);
    assert(*buffer == UBUF_PREPEND);
    assert(size == UBUF_SIZE - UBUF_PREPEND);

    assert(uref_block_resize(&uref1, ubuf_mgr, 2 * UBUF_PREPEND, -UBUF_PREPEND));
    buffer = uref_block_buffer(uref1, &size);
    assert(*buffer == 0);
    assert(size == 2 * UBUF_PREPEND);

    assert(uref_block_resize(&uref1, ubuf_mgr, 3 * UBUF_PREPEND, -UBUF_PREPEND));
    uint64_t offset;
    assert(!uref_block_get_offset(uref1, &offset));
    assert(uref1->ubuf != uref2->ubuf);
    buffer = uref_block_buffer(uref1, &size);
    assert(buffer == uref1->ubuf->planes[0].buffer);
    assert(buffer[2 * UBUF_PREPEND] == UBUF_PREPEND);
    assert(size == 3 * UBUF_PREPEND);

    uref_release(uref1);
    uref_release(uref2);

    assert(urefcount_single(&uref_mgr->refcount));
    uref_mgr_release(uref_mgr);
    assert(urefcount_single(&ubuf_mgr->refcount));
    ubuf_mgr_release(ubuf_mgr);
    return 0;
}

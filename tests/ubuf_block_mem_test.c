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
 * @short unit tests for ubuf manager for block formats
 */

#undef NDEBUG

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_stream.h>
#include <upipe/ubuf_block_mem.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UBUF_POOL_DEPTH     1
#define UBUF_PREPEND        32
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define UBUF_SIZE           188

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct ubuf_mgr *mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                    UBUF_POOL_DEPTH, umem_mgr,
                                                    UBUF_ALIGN,
                                                    UBUF_ALIGN_OFFSET);
    assert(mgr != NULL);

    struct ubuf *ubuf1, *ubuf2, *ubuf3;
    ubuf1 = ubuf_block_alloc(mgr, UBUF_SIZE);
    assert(ubuf1 != NULL);

    size_t size;
    ubase_assert(ubuf_block_size(ubuf1, &size));
    assert(size == UBUF_SIZE);

    const uint8_t *r;
    uint8_t *w;
    int wanted;
    wanted = -1;
    ubase_assert(ubuf_block_read(ubuf1, 0, &wanted, &r));
    assert(wanted == UBUF_SIZE);
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    wanted = -1;
    ubase_assert(ubuf_block_write(ubuf1, 0, &wanted, &w));
    assert(wanted == UBUF_SIZE);
    for (int i = 0; i < UBUF_SIZE; i++)
        w[i] = i + 1;
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    wanted = 1;
    ubase_assert(ubuf_block_read(ubuf1, 42, &wanted, &r));
    assert(wanted == 1);
    assert(*r == 43);
    ubase_assert(ubuf_block_unmap(ubuf1, 42));

    /* test ubuf_block_merge */
    ubase_assert(ubuf_block_merge(mgr, &ubuf1, -2 * UBUF_PREPEND, UBUF_SIZE + 3 * UBUF_PREPEND));
    wanted = -1;
    ubase_assert(ubuf_block_read(ubuf1, 0, &wanted, &r));
    assert(wanted == UBUF_SIZE + 3 * UBUF_PREPEND);
    assert(r[2 * UBUF_PREPEND] == 1);
    assert(r[UBUF_SIZE + 2 * UBUF_PREPEND - 1] == UBUF_SIZE);
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    wanted = 1;
    ubase_assert(ubuf_block_write(ubuf1, 0, &wanted, &w));
    assert(wanted == 1);
    w[0] = 0xAB;
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    /* test ubuf_block_resize */
    ubase_assert(ubuf_block_resize(ubuf1, 2 * UBUF_PREPEND, UBUF_SIZE));
    wanted = -1;
    ubase_assert(ubuf_block_read(ubuf1, 0, &wanted, &r));
    assert(wanted == UBUF_SIZE);
    assert(r[0] == 1);
    assert(r[UBUF_SIZE - 1] == UBUF_SIZE);
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    ubuf_free(ubuf1);


    ubuf1 = ubuf_block_alloc(mgr, 32);
    assert(ubuf1 != NULL);
    wanted = -1;
    ubase_assert(ubuf_block_write(ubuf1, 0, &wanted, &w));
    assert(wanted == 32);
    for (int i = 0; i < 16; i++)
        w[i] = 16 + i;
    for (int i = 16; i < 32; i++)
        w[i] = 17 + i;
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    ubuf2 = ubuf_block_alloc(mgr, 1);
    assert(ubuf2 != NULL);
    wanted = 1;
    ubase_assert(ubuf_block_write(ubuf2, 0, &wanted, &w));
    assert(wanted == 1);
    w[0] = 32;
    ubase_assert(ubuf_block_unmap(ubuf2, 0));
    ubuf3 = ubuf_dup(ubuf2);
    assert(ubuf3 != NULL);
    ubase_assert(ubuf_block_insert(ubuf1, 16, ubuf2));
    /* ubuf2 pointer is now invalid */

    ubase_assert(ubuf_block_size(ubuf1, &size));
    assert(size == 33);

    ubuf2 = ubuf_block_alloc(mgr, 16);
    assert(ubuf2 != NULL);
    wanted = -1;
    ubase_assert(ubuf_block_write(ubuf2, 0, &wanted, &w));
    assert(wanted == 16);
    for (int i = 0; i < 16; i++)
        w[i] = i;
    ubase_assert(ubuf_block_unmap(ubuf2, 0));
    ubase_assert(ubuf_block_insert(ubuf1, 0, ubuf2));
    /* ubuf2 pointer is now invalid */

    ubase_assert(ubuf_block_size(ubuf1, &size));
    assert(size == 49);

    ubuf2 = ubuf_block_alloc(mgr, 17);
    assert(ubuf2 != NULL);
    wanted = -1;
    ubase_assert(ubuf_block_write(ubuf2, 0, &wanted, &w));
    assert(wanted == 17);
    for (int i = 0; i < 16; i++)
        w[i] = i + 49;
    ubase_assert(ubuf_block_unmap(ubuf2, 0));
    ubase_assert(ubuf_block_append(ubuf1, ubuf2));
    /* ubuf2 pointer is now invalid */

    ubase_assert(ubuf_block_size(ubuf1, &size));
    assert(size == 66);

    wanted = 32;
    ubase_assert(ubuf_block_read(ubuf1, 0, &wanted, &r));
    assert(wanted == 16);
    ubase_assert(ubuf_block_unmap(ubuf1, 0));

    /* test ubuf_block_truncate */
    ubase_assert(ubuf_block_truncate(ubuf1, 65));
    ubase_assert(ubuf_block_size(ubuf1, &size));
    assert(size == 65);

    /* test ubuf_block_splice */
    ubuf2 = ubuf_block_splice(ubuf1, 0, -1);
    assert(ubuf2 != NULL);
    ubuf_free(ubuf2);

    ubuf2 = ubuf_block_splice(ubuf1, 49, -1);
    assert(ubuf2 != NULL);

    wanted = -1;
    ubase_nassert(ubuf_block_write(ubuf2, 0, &wanted, &w));
    ubase_assert(ubuf_block_read(ubuf2, 0, &wanted, &r));
    assert(wanted == 16);
    for (int i = 0; i < 16; i++)
        assert(r[i] == i + 49);
    ubase_assert(ubuf_block_unmap(ubuf2, 0));
    ubuf_free(ubuf2);

    /* test ubuf_block_peek */
    uint8_t buffer[4];
    r = ubuf_block_peek(ubuf1, 30, 4, buffer);
    assert(r == buffer);
    assert(r[0] == 30 && r[3] == 33);
    ubase_assert(ubuf_block_peek_unmap(ubuf1, 30, buffer, r));

    r = ubuf_block_peek(ubuf1, 0, 4, buffer);
    assert(r != NULL);
    assert(r != buffer);
    assert(r[0] == 0 && r[3] == 3);
    ubase_assert(ubuf_block_peek_unmap(ubuf1, 0, buffer, r));

    /* test refcounting */
    wanted = -1;
    ubase_nassert(ubuf_block_write(ubuf1, 32, &wanted, &w));

    ubuf_free(ubuf3);
    wanted = -1;
    ubase_assert(ubuf_block_write(ubuf1, 32, &wanted, &w));
    assert(wanted == 1);
    ubase_assert(ubuf_block_unmap(ubuf1, 32));

    /* test ubuf_block_copy */
    ubuf2 = ubuf_block_copy(mgr, ubuf1, 1, -1);
    assert(ubuf2 != NULL);
    wanted = -1;
    ubase_assert(ubuf_block_read(ubuf2, 0, &wanted, &r));
    assert(wanted == 64);
    for (int i = 0; i < wanted; i++)
        assert(r[i] == i + 1);
    ubase_assert(ubuf_block_unmap(ubuf2, 0));
    ubuf_free(ubuf2);

    /* test ubuf_block_equal */
    ubuf2 = ubuf_block_copy(mgr, ubuf1, 0, -1);
    assert(ubuf2 != NULL);
    ubase_assert(ubuf_block_equal(ubuf1, ubuf2));
    ubuf_free(ubuf2);

    /* test ubuf_block_match */
    uint8_t filter[] = { 0, 1, 2, 1 };
    uint8_t mask[] = { 0xff, 0xff, 0x0f, 0xfd };
    ubase_assert(ubuf_block_match(ubuf1, filter, mask, 4));
    filter[3] = 0;
    ubase_nassert(ubuf_block_match(ubuf1, filter, mask, 4));

    /* test ubuf_block_scan */
    size_t offset = 2;
    ubase_assert(ubuf_block_scan(ubuf1, &offset, 3));
    assert(offset == 3);

    /* test ubuf_block_find */
    offset = 0;
    ubase_assert(ubuf_block_find(ubuf1, &offset, 2, 2, 3));
    assert(offset == 2);

    /* test ubuf_block_stream */
    struct ubuf_block_stream s;
    ubuf_block_stream_init(&s, ubuf1, 0);
    ubuf_block_stream_fill_bits(&s, 24);
    uint32_t bits = ubuf_block_stream_show_bits(&s, 1);
    ubuf_block_stream_skip_bits(&s, 1);
    assert(!bits);
    for (int i = 0; i < 64; i++) {
        ubuf_block_stream_fill_bits(&s, 8);
        bits = ubuf_block_stream_show_bits(&s, 8);
        ubuf_block_stream_skip_bits(&s, 8);
        assert(bits == i << 1);
    }
    ubuf_block_stream_clean(&s);

    /* test ubuf_block_delete */
    ubase_assert(ubuf_block_delete(ubuf1, 8, 32));
    uint8_t buf[33];
    ubase_assert(ubuf_block_extract(ubuf1, 0, -1, buf));
    for (int i = 0; i < 8; i++)
        assert(buf[i] == i);
    for (int i = 9; i < 33; i++)
        assert(buf[i] == i + 32);
    ubuf_free(ubuf1);

    ubuf_mgr_release(mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

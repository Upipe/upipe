/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
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
 * @short unit tests for ubuf manager for sound formats
 */

#undef NDEBUG

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_sound.h>
#include <upipe/ubuf_sound_mem.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UBUF_POOL_DEPTH     1

static void fill_in(struct ubuf *ubuf)
{
    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(ubuf, &size, &sample_size));
    int octets = size * sample_size;

    const char *channel = NULL;
    while (ubase_check(ubuf_sound_plane_iterate(ubuf, &channel)) &&
           channel != NULL) {
        uint8_t *buffer;
        ubase_assert(ubuf_sound_plane_write_uint8_t(ubuf, channel, 0, -1,
                                                    &buffer));

        for (int x = 0; x < octets; x++)
            buffer[x] = (uint8_t)channel[0] + x;
        ubase_assert(ubuf_sound_plane_unmap(ubuf, channel, 0, -1));
    }
}

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);

    struct ubuf_mgr *mgr;
    struct ubuf *ubuf1, *ubuf2;
    const char *channel;
    size_t size;
    uint8_t sample_size;
    uint8_t *w;
    const uint8_t *r;

    /* packed s16 stereo */
    mgr = ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr,
                                   4, 32);
    assert(mgr != NULL);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "lr"));

    ubuf1 = ubuf_sound_alloc(mgr, 32);
    assert(ubuf1 != NULL);

    ubase_assert(ubuf_sound_size(ubuf1, &size, &sample_size));
    assert(size == 32);
    assert(sample_size == 4);

    channel = NULL;
    unsigned int nb_planes = 0;
    while (ubase_check(ubuf_sound_plane_iterate(ubuf1, &channel)) &&
           channel != NULL) {
        nb_planes++;
        assert(!strcmp(channel, "lr"));
    }
    assert(nb_planes == 1);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "lr", 0, -1, &r));
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "lr", 0, -1));

    fill_in(ubuf1);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "lr", 2, 1, &r));
    assert(*r == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "lr", 2, 1));

    ubuf2 = ubuf_dup(ubuf1);
    assert(ubuf2 != NULL);
    ubase_nassert(ubuf_sound_plane_write_uint8_t(ubuf1, "lr", 0, -1, &w));
    ubuf_free(ubuf2);

    ubase_nassert(ubuf_sound_resize(ubuf1, 0, 33));

    ubase_assert(ubuf_sound_resize(ubuf1, 2, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "lr", 0, -1, &r));
    assert(r[0] == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "lr", 0, -1));

    ubase_assert(ubuf_sound_resize(ubuf1, 0, 29));

    ubuf_free(ubuf1);

    ubuf_mgr_release(mgr);

    /* planar float 5.1 */
    mgr = ubuf_sound_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr,
                                   sizeof(float), 32);
    assert(mgr != NULL);
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "l"));
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "r"));
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "c"));
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "L"));
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "R"));
    ubase_assert(ubuf_sound_mem_mgr_add_plane(mgr, "S"));

    ubuf1 = ubuf_sound_alloc(mgr, 32);
    assert(ubuf1 != NULL);

    ubase_assert(ubuf_sound_size(ubuf1, &size, &sample_size));
    assert(size == 32);
    assert(sample_size == sizeof(float));

    channel = NULL;
    nb_planes = 0;
    while (ubase_check(ubuf_sound_plane_iterate(ubuf1, &channel)) &&
           channel != NULL)
        nb_planes++;
    assert(nb_planes == 6);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "l", 0, -1, &r));
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "l", 0, -1));

    fill_in(ubuf1);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "l", 2, 1, &r));
    assert(*r == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "l", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "r", 2, 1, &r));
    assert(*r == 'r' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "r", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "c", 2, 1, &r));
    assert(*r == 'c' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "c", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "L", 2, 1, &r));
    assert(*r == 'L' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "L", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "R", 2, 1, &r));
    assert(*r == 'R' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "R", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "S", 2, 1, &r));
    assert(*r == 'S' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "S", 2, 1));

    ubuf2 = ubuf_dup(ubuf1);
    assert(ubuf2 != NULL);
    ubase_nassert(ubuf_sound_plane_write_uint8_t(ubuf1, "l", 0, -1, &w));
    ubuf_free(ubuf2);

    ubase_nassert(ubuf_sound_resize(ubuf1, 0, 33));

    ubase_assert(ubuf_sound_resize(ubuf1, 2, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "l", 0, -1, &r));
    assert(r[0] == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "l", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "r", 0, -1, &r));
    assert(r[0] == 'r' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "r", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "c", 0, -1, &r));
    assert(r[0] == 'c' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "c", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "L", 0, -1, &r));
    assert(r[0] == 'L' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "L", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "R", 0, -1, &r));
    assert(r[0] == 'R' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "R", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "S", 0, -1, &r));
    assert(r[0] == 'S' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "S", 0, -1));

    ubase_assert(ubuf_sound_resize(ubuf1, 0, 29));

    ubuf_free(ubuf1);

    ubuf_mgr_release(mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

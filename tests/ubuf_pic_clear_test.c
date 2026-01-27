/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for picture clear
 */

#undef NDEBUG

#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_pic.h"
#include "upipe/ubuf_pic_mem.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <time.h>
#include <inttypes.h>

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

    const char *chroma;
    ubuf_pic_foreach_plane(ubuf, chroma) {
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

static void check(struct ubuf *ubuf, const char *chroma,
                  uint8_t *pattern, size_t pattern_size)
{
    size_t hsize, vsize;
    uint8_t macropixel;
    ubase_assert(ubuf_pic_size(ubuf, &hsize, &vsize, &macropixel));

    size_t stride;
    uint8_t hsub, vsub, macropixel_size;
    ubase_assert(ubuf_pic_plane_size(ubuf, chroma, &stride, &hsub, &vsub,
                                     &macropixel_size));
    int hoctets = hsize * macropixel_size / hsub / macropixel;
    const uint8_t *buffer;
    ubase_assert(ubuf_pic_plane_read(ubuf, chroma, 0, 0, -1, -1, &buffer));

    for (int y = 0; y < vsize / vsub; y++) {
        for (int x = 0; x < hoctets; x += pattern_size)
            assert(!memcmp(buffer + x, pattern, pattern_size));
        buffer += stride;
    }
    ubase_assert(ubuf_pic_plane_unmap(ubuf, chroma, 0, 0, -1, -1));
}

int main(int argc, char **argv)
{
    size_t loops = 0;
    if (argc >= 2)
        loops = atol(argv[1]);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);

    struct ubuf_mgr *mgr;
    struct ubuf *ubuf;

    /* yuv420p */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "u8", 2, 2, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "v8", 2, 2, 1));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "y8", (uint8_t []){ 16 }, 1);
    check(ubuf, "u8", (uint8_t []){ 128 }, 1);
    check(ubuf, "v8", (uint8_t []){ 128 }, 1);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "y8", (uint8_t []){ 0 }, 1);
    check(ubuf, "u8", (uint8_t []){ 128 }, 1);
    check(ubuf, "v8", (uint8_t []){ 128 }, 1);

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    /* yuv422p */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "u8", 2, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "v8", 2, 1, 1));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "y8", (uint8_t []){ 16 }, 1);
    check(ubuf, "u8", (uint8_t []){ 128 }, 1);
    check(ubuf, "v8", (uint8_t []){ 128 }, 1);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "y8", (uint8_t []){ 0 }, 1);
    check(ubuf, "u8", (uint8_t []){ 128 }, 1);
    check(ubuf, "v8", (uint8_t []){ 128 }, 1);

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    /* nv12 */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y8", 1, 1, 1));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "u8v8", 2, 2, 2));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "y8", (uint8_t []){ 16 }, 1);
    check(ubuf, "u8v8", (uint8_t []){ 128 }, 1);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "y8", (uint8_t []){ 0 }, 1);
    check(ubuf, "u8v8", (uint8_t []){ 128 }, 1);

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    /* rgba */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "r8g8b8a8", 1, 1, 4));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "r8g8b8a8", (uint8_t []){ 16, 16, 16, 0 }, 4);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "r8g8b8a8", (uint8_t []){ 0, 0, 0, 0 }, 4);

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    /* yuv420p10le */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "u10l", 2, 2, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "v10l", 2, 2, 2));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "y10l", (uint8_t []){ 64, 0 }, 2);
    check(ubuf, "u10l", (uint8_t []){ 0, 2 }, 2);
    check(ubuf, "v10l", (uint8_t []){ 0, 2 }, 2);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "y10l", (uint8_t []){ 0, 0 }, 2);
    check(ubuf, "u10l", (uint8_t []){ 0, 2 }, 2);
    check(ubuf, "v10l", (uint8_t []){ 0, 2 }, 2);

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    /* yuv422p10le */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "y10l", 1, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "u10l", 2, 1, 2));
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr, "v10l", 2, 1, 2));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "y10l", (uint8_t []){ 64, 0 }, 2);
    check(ubuf, "u10l", (uint8_t []){ 0, 2 }, 2);
    check(ubuf, "v10l", (uint8_t []){ 0, 2 }, 2);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "y10l", (uint8_t []){ 0, 0 }, 2);
    check(ubuf, "u10l", (uint8_t []){ 0, 2 }, 2);
    check(ubuf, "v10l", (uint8_t []){ 0, 2 }, 2);

#if 1
    struct timespec t, t0, tp;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
    tp = t0;

    for (size_t l = 0; l < loops; /* do nothing */) {
        ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0);

        if (++l % 8192 == 0) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &t);
            uint64_t t_diff = t.tv_sec * UINT64_C(1000000000) + t.tv_nsec
                - (tp.tv_sec * UINT64_C(1000000000) + tp.tv_nsec);
            uint64_t t0_diff = t.tv_sec * UINT64_C(1000000000) + t.tv_nsec
                - (t0.tv_sec * UINT64_C(1000000000) + t0.tv_nsec);

            printf("%"PRIu64" calls to ubuf_pic_clear per second, avg: %"PRIu64"\n",
                    8192 * UINT64_C(1000000000) / t_diff,
                    l * UINT64_C(1000000000) / t0_diff);
            tp = t;
        }
    }
#endif

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    /* v210 */
    mgr = ubuf_pic_mem_mgr_alloc(UBUF_POOL_DEPTH, UBUF_POOL_DEPTH, umem_mgr, 1,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_PREPEND, UBUF_APPEND,
                                 UBUF_ALIGN, UBUF_ALIGN_HOFFSET);
    assert(mgr != NULL);
    ubase_assert(ubuf_pic_mem_mgr_add_plane(mgr,
        "u10y10v10y10u10y10v10y10u10y10v10y10", 1, 1, 16));

    ubuf = ubuf_pic_alloc(mgr, 1920, 1080);
    assert(ubuf != NULL);
    fill_in(ubuf);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 0));
    check(ubuf, "u10y10v10y10u10y10v10y10u10y10v10y10",
          (uint8_t []){ 0, 66, 0, 32, 16, 0, 8, 1 }, 8);

    ubase_assert(ubuf_pic_clear(ubuf, 0, 0, -1, -1, 1));
    check(ubuf, "u10y10v10y10u10y10v10y10u10y10v10y10",
          (uint8_t []){ 0, 2, 0, 32, 0, 0, 8, 0 }, 8);

    ubuf_free(ubuf);
    ubuf_mgr_release(mgr);

    umem_mgr_release(umem_mgr);
    return 0;
}

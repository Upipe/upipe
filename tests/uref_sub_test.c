/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for urefs carrying multiple ubufs
 */

#undef NDEBUG

#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/ubuf.h"
#include "upipe/ubuf_block.h"
#include "upipe/ubuf_block_mem.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/uref_flow.h"
#include "upipe/uref_sub.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 5
#define UREF_POOL_DEPTH 5
#define UBUF_POOL_DEPTH 5

/** @This allocates a block ubuf filled with the given octet. */
static struct ubuf *build_block(struct ubuf_mgr *mgr, uint8_t fill, int size)
{
    struct ubuf *ubuf = ubuf_block_alloc(mgr, size);
    assert(ubuf != NULL);
    int wsize = -1;
    uint8_t *w;
    ubase_assert(ubuf_block_write(ubuf, 0, &wsize, &w));
    assert(wsize == size);
    memset(w, fill, wsize);
    ubase_assert(ubuf_block_unmap(ubuf, 0));
    return ubuf;
}

/** @This checks that a block ubuf is filled with the given octet. */
static void check_block(struct ubuf *ubuf, uint8_t fill, int size)
{
    assert(ubuf != NULL);
    int rsize = -1;
    const uint8_t *r;
    ubase_assert(ubuf_block_read(ubuf, 0, &rsize, &r));
    assert(rsize == size);
    for (int i = 0; i < rsize; i++)
        assert(r[i] == fill);
    ubase_assert(ubuf_block_unmap(ubuf, 0));
}

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, 0, 0, 0, 0);
    assert(ubuf_mgr != NULL);

    /* attach primary + two additional ubufs */
    struct uref *uref = uref_alloc(mgr);
    assert(uref != NULL);
    assert(uref_sub_count(uref) == 0);
    assert(uref_sub_get(uref, 0) == NULL);
    assert(uref_sub_get(uref, 1) == NULL);

    uref_attach_ubuf(uref, build_block(ubuf_mgr, 'V', 64));
    assert(uref_sub_count(uref) == 1);
    assert(uref_sub_get(uref, 0) == uref->ubuf);

    assert(uref_sub_attach_ubuf(uref, build_block(ubuf_mgr, 'A', 32)) == 1);
    assert(uref_sub_attach_ubuf(uref, build_block(ubuf_mgr, 'S', 16)) == 2);
    assert(uref_sub_count(uref) == 3);
    check_block(uref_sub_get(uref, 0), 'V', 64);
    check_block(uref_sub_get(uref, 1), 'A', 32);
    check_block(uref_sub_get(uref, 2), 'S', 16);
    assert(uref_sub_get(uref, 3) == NULL);

    /* per-entry attributes */
    ubase_assert(uref_sub_set_flow_id(uref, 0x100, 0));
    ubase_assert(uref_sub_set_flow_id(uref, 0x101, 1));
    ubase_assert(uref_sub_set_flow_id(uref, 0x102, 2));
    ubase_assert(uref_sub_set_def(uref, "sound.s32.", 1));

    uint8_t index = 0xff;
    struct ubuf *found = uref_sub_find_flow_id(uref, 0x101, &index);
    assert(index == 1);
    check_block(found, 'A', 32);
    assert(uref_sub_find_flow_id(uref, 0xdead, NULL) == NULL);

    /* uref_dup duplicates the whole chain and its attributes */
    struct uref *dup = uref_dup(uref);
    assert(dup != NULL);
    assert(uref_sub_count(dup) == 3);
    assert(uref_sub_get(dup, 1) != uref_sub_get(uref, 1));
    check_block(uref_sub_get(dup, 0), 'V', 64);
    check_block(uref_sub_get(dup, 1), 'A', 32);
    check_block(uref_sub_get(dup, 2), 'S', 16);
    uint64_t v;
    ubase_assert(uref_sub_get_flow_id(dup, &v, 2));
    assert(v == 0x102);
    const char *def;
    ubase_assert(uref_sub_get_def(dup, &def, 1));
    assert(!strcmp(def, "sound.s32."));
    uref_free(dup);

    /* uref_fork replaces the primary ubuf and keeps the chain */
    struct uref *fork = uref_fork(uref, build_block(ubuf_mgr, 'W', 8));
    assert(fork != NULL);
    assert(uref_sub_count(fork) == 3);
    check_block(uref_sub_get(fork, 0), 'W', 8);
    check_block(uref_sub_find_flow_id(fork, 0x101, NULL), 'A', 32);
    uref_free(fork);

    /* replace an entry in place */
    ubase_assert(uref_sub_replace_ubuf(uref, build_block(ubuf_mgr, 'B', 32),
                                       1));
    check_block(uref_sub_get(uref, 1), 'B', 32);
    assert(uref_sub_count(uref) == 3);
    ubase_nassert(uref_sub_replace_ubuf(uref, build_block(ubuf_mgr, 'X', 8),
                                        42));

    /* extract an entry as a standalone uref: per-entry attributes become
     * plain attributes again, visible to shorthand accessors */
    uref->date_prog = 42;
    struct uref *extracted = uref_sub_extract(uref, 1);
    assert(extracted != NULL);
    assert(uref_sub_count(extracted) == 1);
    check_block(extracted->ubuf, 'B', 32);
    assert(extracted->date_prog == 42);
    ubase_assert(uref_flow_get_id(extracted, &v));
    assert(v == 0x101);
    ubase_assert(uref_flow_get_def(extracted, &def));
    assert(!strcmp(def, "sound.s32."));
    uref_free(extracted);

    /* extract entry 0: its own namespace, without other entries' */
    extracted = uref_sub_extract(uref, 0);
    assert(extracted != NULL);
    check_block(extracted->ubuf, 'V', 64);
    ubase_assert(uref_flow_get_id(extracted, &v));
    assert(v == 0x100);
    ubase_nassert(uref_sub_get_flow_id(extracted, &v, 1));
    uref_free(extracted);

    assert(uref_sub_extract(uref, 42) == NULL);

    /* merge a standalone uref as a new entry */
    struct uref *sound = uref_alloc(mgr);
    assert(sound != NULL);
    ubase_nassert(uref_sub_merge(uref, sound, NULL)); /* no ubuf */
    uref_attach_ubuf(sound, build_block(ubuf_mgr, 'M', 24));
    ubase_assert(uref_flow_set_id(sound, 0x200));
    ubase_assert(uref_flow_set_def(sound, "sound.s16."));
    uint8_t merge_index = 0;
    ubase_assert(uref_sub_merge(uref, sound, &merge_index));
    assert(merge_index == 3);
    assert(uref_sub_count(uref) == 4);
    check_block(uref_sub_find_flow_id(uref, 0x200, &merge_index), 'M', 24);
    assert(merge_index == 3);
    ubase_assert(uref_sub_get_def(uref, &def, 3));
    assert(!strcmp(def, "sound.s16."));

    /* detach an additional entry: its attributes are deleted and following
     * entries' attributes are renumbered */
    struct ubuf *detached = uref_sub_detach_ubuf(uref, 1);
    check_block(detached, 'B', 32);
    ubuf_free(detached);
    assert(uref_sub_count(uref) == 3);
    check_block(uref_sub_get(uref, 1), 'S', 16);
    ubase_assert(uref_sub_get_flow_id(uref, &v, 1));
    assert(v == 0x102);
    ubase_nassert(uref_sub_get_def(uref, &def, 1));
    ubase_assert(uref_sub_get_flow_id(uref, &v, 2));
    assert(v == 0x200);
    ubase_assert(uref_sub_get_def(uref, &def, 2));
    assert(!strcmp(def, "sound.s16."));
    ubase_nassert(uref_sub_get_flow_id(uref, &v, 3));
    assert(uref_sub_find_flow_id(uref, 0x101, NULL) == NULL);
    check_block(uref_sub_find_flow_id(uref, 0x200, NULL), 'M', 24);
    assert(uref_sub_detach_ubuf(uref, 3) == NULL);

    /* legacy detach only touches the primary ubuf */
    detached = uref_detach_ubuf(uref);
    check_block(detached, 'V', 64);
    ubuf_free(detached);
    assert(uref_sub_get(uref, 0) == NULL);
    check_block(uref_sub_get(uref, 1), 'S', 16);

    /* uref_free releases the remaining chain (checked by refcounts on
     * manager release below) */
    uref_free(uref);

    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

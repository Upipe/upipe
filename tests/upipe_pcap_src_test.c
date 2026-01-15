/*
 * Copyright (c) 2026 Open Broadcast Systems Ltd.
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for pcap source pipe
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_block.h"
#include "upipe/uref_std.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"
#include "upipe/upipe.h"
#include "upipe-modules/upipe_null.h"
#include "upipe-modules/upipe_probe_uref.h"
#include "upipe-pcap/upipe_pcap_src.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s <source files>\n", argv0);
    exit(EXIT_FAILURE);
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    return UBASE_ERR_NONE;
}

enum {
    SOURCE_NO_URI = 1,
    SOURCE_RELEASE = 3,
    SOURCE_RESET_NO_URI = 5,
    SOURCE_MAX
};

unsigned source_nb = SOURCE_MAX + 1;

static int catch_probe_uref(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    struct uref *uref = NULL;
    struct upump **upump_p = NULL;
    bool *drop = NULL;

    if (uprobe_probe_uref_check(event, args, &uref, &upump_p, &drop)) {
        size_t s;
        ubase_assert(uref_block_size(uref, &s));
        upipe_notice_va(upipe, "packet size %zu", s);
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}


int main(int argc, char *argv[])
{
    if (argc != 2)
        usage(argv[0]);
    const char *file = argv[1];

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL,
            UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger =
        uprobe_stdio_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    struct upipe_mgr *upipe_pcap_src_mgr = upipe_pcap_src_mgr_alloc();
    assert(upipe_pcap_src_mgr != NULL);

    struct upipe *sources[source_nb];
    for (unsigned i = 0; i < source_nb; i++) {
        sources[i] = upipe_void_alloc(
            upipe_pcap_src_mgr,
            uprobe_pfx_alloc_va(uprobe_use(logger),
                                UPROBE_LOG_INFO, "pcap %u", i));
        assert(sources[i]);
    }
    upipe_mgr_release(upipe_pcap_src_mgr);

    for (unsigned i = 0; i < source_nb; i++) {
        if (i != SOURCE_NO_URI)
            ubase_assert(upipe_set_uri(sources[i], file));

        struct upipe *upipe;
        upipe = upipe_use(sources[i]);

        struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
        assert(upipe_probe_uref_mgr != NULL);
        struct upipe *output = upipe_void_chain_output(
                upipe, upipe_probe_uref_mgr,
                uprobe_pfx_alloc_va(uprobe_alloc(catch_probe_uref, uprobe_use(logger)),
                                    UPROBE_LOG_INFO, "probe_uref %u", i));
        assert(output != NULL);

        struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
        assert(upipe_null_mgr != NULL);
        output = upipe_void_chain_output(
                output, upipe_null_mgr,
                uprobe_pfx_alloc_va(uprobe_use(logger),
                                    UPROBE_LOG_LEVEL, "null %u", i));
        assert(output != NULL);
        upipe_release(output);

        if (i == SOURCE_RELEASE) {
            upipe_release(sources[i]);
            sources[i] = NULL;
        }
        if (i == SOURCE_RESET_NO_URI)
            ubase_assert(upipe_set_uri(sources[i], NULL));
    }

    upump_mgr_run(upump_mgr, NULL);

    for (unsigned i = 0; i < source_nb; i++)
        upipe_release(sources[i]);

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

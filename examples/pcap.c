/*
 * Copyright (C) 2025 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uprobe_uref_mgr.h"
#include "upipe/uprobe_uclock.h"
#include "upipe/uprobe_upump_mgr.h"
#include "upipe/uprobe_ubuf_mem.h"
#include "upipe/umem.h"
#include "upipe/uclock.h"
#include "upipe/uclock_std.h"
#include "upipe/umem_pool.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/uref.h"
#include "upipe/uref_std.h"
#include "upipe/upipe.h"
#include "upipe/upump.h"
#include "upump-ev/upump_ev.h"
#include "upipe-pcap/upipe_pcap_src.h"
#include "upipe-modules/upipe_null.h"

#define UPROBE_LOG_LEVEL UPROBE_LOG_INFO
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <input>\n", argv[0]);
        exit(-1);
    }

    const char *input = argv[1];

    /* structures managers */
    struct upump_mgr *upump_mgr =
        upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr =
        udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr =
        uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    udict_mgr_release(udict_mgr);

    struct uclock *uclock = uclock_std_alloc(0);

    /* probes */
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_alloc(NULL, stderr, UPROBE_LOG_DEBUG);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(uprobe != NULL);
    uprobe = uprobe_uclock_alloc(uprobe, uclock);
    assert(uprobe != NULL);

    uref_mgr_release(uref_mgr);
    upump_mgr_release(upump_mgr);
    umem_mgr_release(umem_mgr);

    /* pipes */

    struct upipe_mgr *upipe_pcap_src_mgr = upipe_pcap_src_mgr_alloc();
    struct upipe *upipe_src = upipe_void_alloc(upipe_pcap_src_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "pcap"));
    upipe_mgr_release(upipe_pcap_src_mgr);

    upipe_attach_uclock(upipe_src);

    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    struct upipe *upipe = upipe_void_alloc_output(upipe_src, upipe_null_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "null"));
    upipe_mgr_release(upipe_null_mgr);
    upipe_release(upipe);

    if (!ubase_check(upipe_set_uri(upipe_src, input))) {
        fprintf(stderr, "invalid input\n");
        return EXIT_FAILURE;
    }

    uprobe_release(uprobe);

    /* main loop */
    upump_mgr_run(upump_mgr, NULL);

    uclock_release(uclock);
    upipe_release(upipe_src);

    return 0;
}

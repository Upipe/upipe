/*
 * Copyright (C) 2016-2019 Open Broadcast Systems Ltd.
 *
 * Authors: Rafaël Carré, James Darnley
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

#include <stdlib.h>
#include <stdio.h>

#include <upipe/ubase.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/upipe.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/upump.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>

#include <upump-ev/upump_ev.h>

#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_dtsdi.h>
#include <upipe-modules/upipe_null.h>

#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10

static int loglevel = UPROBE_LOG_DEBUG;

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s file.dtsdi\n", argv[0]);
        return 0;
    }

    /* structures managers */
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    assert(umem_mgr);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr);

    /* probes */
    struct uprobe *uprobe = uprobe_stdio_alloc(NULL, stderr, loglevel);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH, UBUF_SHARED_POOL_DEPTH);
    assert(uprobe != NULL);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    assert(uprobe != NULL);

    /* file source */
    struct upipe_mgr *fsrc_mgr = upipe_fsrc_mgr_alloc();
    struct upipe *pipe_fsrc = upipe_void_alloc(fsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe), loglevel, "fsrc"));
    assert(pipe_fsrc);
    upipe_mgr_release(fsrc_mgr);
    upipe_set_uri(pipe_fsrc, argv[1]);

    /* dtsdi */
    struct upipe_mgr *dtsdi_mgr = upipe_dtsdi_mgr_alloc();
    struct upipe *pipe_dtsdi = upipe_void_alloc(dtsdi_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe), loglevel, "dtsdi"));
    assert(pipe_dtsdi);
    upipe_mgr_release(dtsdi_mgr);

    /* null */
    struct upipe_mgr *null_mgr = upipe_null_mgr_alloc();
    struct upipe *pipe_null = upipe_void_alloc(null_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe), loglevel, "null"));
    assert(pipe_null);
    upipe_mgr_release(null_mgr);

    ubase_assert(upipe_set_output(pipe_fsrc, pipe_dtsdi));
    ubase_assert(upipe_set_output(pipe_dtsdi, pipe_null));

    upump_mgr_run(upump_mgr, NULL);

    upipe_release(pipe_null);
    upipe_release(pipe_dtsdi);
    upipe_release(pipe_fsrc);

    uprobe_release(uprobe);

    udict_mgr_release(udict_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    upump_mgr_release(upump_mgr);

    return 0;
}

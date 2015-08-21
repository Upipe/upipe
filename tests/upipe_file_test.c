/*
 * Copyright (C) 2012-2015 OpenHeadend S.A.R.L.
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
 * @short unit tests for file source and sink pipes
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_file_sink.h>
#include <upipe-modules/upipe_delay.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <ev.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0
#define READ_SIZE 4096
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static void usage(const char *argv0) {
    fprintf(stdout, "Usage: %s [-d <delay>] [-a|-o] <source file> <sink file>\n", argv0);
    fprintf(stdout, "-a : append\n");
    fprintf(stdout, "-o : overwrite\n");
    exit(EXIT_FAILURE);
}

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
        case UPROBE_SOURCE_END:
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    const char *src_file, *sink_file;
    uint64_t delay = 0;
    enum upipe_fsink_mode mode = UPIPE_FSINK_CREATE;
    int opt;
    while ((opt = getopt(argc, argv, "d:ao")) != -1) {
        switch (opt) {
            case 'd':
                delay = atoi(optarg);
                break;
            case 'a':
                mode = UPIPE_FSINK_APPEND;
                break;
            case 'o':
                mode = UPIPE_FSINK_OVERWRITE;
                break;
            default:
                usage(argv[0]);
        }
    }
    if (optind >= argc -1)
        usage(argv[0]);
    src_file = argv[optind++];
    sink_file = argv[optind++];

    struct ev_loop *loop = ev_default_loop(0);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_LEVEL);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);
    if (delay) {
        logger = uprobe_uclock_alloc(logger, uclock);
        assert(logger != NULL);
    }

    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    assert(upipe_fsrc_mgr != NULL);
    struct upipe *upipe_fsrc = upipe_void_alloc(upipe_fsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(logger),
                             UPROBE_LOG_LEVEL, "file source"));
    assert(upipe_fsrc != NULL);
    ubase_assert(upipe_set_output_size(upipe_fsrc, READ_SIZE));
    ubase_assert(upipe_set_uri(upipe_fsrc, src_file));
    uint64_t size;
    if (ubase_check(upipe_src_get_size(upipe_fsrc, &size)))
        fprintf(stdout, "source file has size %"PRIu64"\n", size);
    else
        fprintf(stdout, "source path is not a regular file\n");

    struct upipe *upipe;
    if (delay) {
        ubase_assert(upipe_attach_uclock(upipe_fsrc));
        struct upipe_mgr *upipe_delay_mgr = upipe_delay_mgr_alloc();
        assert(upipe_delay_mgr != NULL);
        upipe = upipe_void_alloc_output(upipe_fsrc,
                upipe_delay_mgr,
                uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                 "delay"));
        assert(upipe != NULL);
        upipe_delay_set_delay(upipe, delay);
    } else
        upipe = upipe_use(upipe_fsrc);

    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    assert(upipe_fsink_mgr != NULL);
    struct upipe *upipe_fsink = upipe_void_chain_output(upipe,
            upipe_fsink_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL,
                             "file sink"));
    assert(upipe_fsink != NULL);
    if (delay)
        ubase_assert(upipe_attach_uclock(upipe_fsink));
    ubase_assert(upipe_fsink_set_path(upipe_fsink, sink_file, mode));
    upipe_release(upipe_fsink);

    ev_loop(loop, 0);

    upipe_release(upipe_fsrc);
    upipe_mgr_release(upipe_fsrc_mgr); // nop
    upipe_mgr_release(upipe_fsink_mgr); // nop

    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uclock_release(uclock);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    ev_default_destroy();
    return 0;
}

/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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
 * @short unit tests for sequential source pipe
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio_color.h>
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
#include <upipe-modules/upipe_delay.h>
#include <upipe-modules/upipe_null.h>
#include <upipe-modules/upipe_sequential_source.h>

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
    fprintf(stdout, "Usage: %s [-d <delay>] <source files>\n", argv0);
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

int main(int argc, char *argv[])
{
    uint64_t delay = 0;
    int opt;
    while ((opt = getopt(argc, argv, "d:ao")) != -1) {
        switch (opt) {
            case 'd':
                delay = atoi(optarg);
                break;
            default:
                usage(argv[0]);
        }
    }
    if (optind > argc - 1)
        usage(argv[0]);
    const char *file = argv[optind];

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
    struct uprobe *logger =
        uprobe_stdio_color_alloc(&uprobe, stdout, UPROBE_LOG_LEVEL);
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

    struct upipe_mgr *upipe_seq_src_mgr = upipe_seq_src_mgr_alloc();
    assert(upipe_seq_src_mgr != NULL);
    {
        struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
        assert(upipe_fsrc_mgr != NULL);
        ubase_assert(upipe_seq_src_mgr_set_source_mgr(
                upipe_seq_src_mgr, upipe_fsrc_mgr));
        upipe_mgr_release(upipe_fsrc_mgr);
    }

    struct upipe *sources[source_nb];
    for (unsigned i = 0; i < source_nb; i++) {
        sources[i] = upipe_void_alloc(
            upipe_seq_src_mgr,
            uprobe_pfx_alloc_va(uprobe_use(logger),
                                UPROBE_LOG_LEVEL, "seq %u", i));
        assert(sources[i]);
    }
    upipe_mgr_release(upipe_seq_src_mgr);

    for (unsigned i = 0; i < source_nb; i++) {
        if (delay)
            ubase_assert(upipe_attach_uclock(sources[i]));

        if (i != SOURCE_NO_URI)
            ubase_assert(upipe_set_uri(sources[i], file));

        struct upipe *upipe;
        if (delay) {
            struct upipe_mgr *upipe_delay_mgr = upipe_delay_mgr_alloc();
            assert(upipe_delay_mgr != NULL);
            upipe = upipe_void_alloc_output(
                sources[i], upipe_delay_mgr,
                uprobe_pfx_alloc_va(uprobe_use(logger), UPROBE_LOG_LEVEL,
                                 "delay %u", i));
            assert(upipe != NULL);
            upipe_delay_set_delay(upipe, delay);
        }
        else
            upipe = upipe_use(sources[i]);

        struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
        assert(upipe_null_mgr != NULL);
        struct upipe *output = upipe_void_chain_output(
                upipe, upipe_null_mgr,
                uprobe_pfx_alloc_va(uprobe_use(logger),
                                    UPROBE_LOG_LEVEL, "null %u", i));
        assert(output != NULL);
        if (delay)
            ubase_assert(upipe_attach_uclock(output));
        upipe_release(output);

        if (i == SOURCE_RELEASE) {
            upipe_release(sources[i]);
            sources[i] = NULL;
        }
        if (i == SOURCE_RESET_NO_URI)
            ubase_assert(upipe_set_uri(sources[i], NULL));
    }

    ev_loop(loop, 0);

    for (unsigned i = 0; i < source_nb; i++)
        upipe_release(sources[i]);

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

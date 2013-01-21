/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/udict_dump.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/uref_clock.h>
#include <upipe-modules/upipe_genaux.h>

#include <upipe/upipe_helper_upipe.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define ALIVE() { printf("# ALIVE: %s %s - %d\n", __FILE__, __func__, __LINE__); } // FIXME - debug - remove this

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          16
#define UBUF_ALIGN_OFFSET   0
#define ULOG_LEVEL ULOG_DEBUG

#define IMGSIZE VHASH_IMGSIZE

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe, enum uprobe_event event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
    }
    return true;
}

/** phony pipe to test upipe_genaux */
struct genaux_test {
    struct uref *flow;
    struct uref *entry;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_genaux */
UPIPE_HELPER_UPIPE(genaux_test, upipe);

/** helper phony pipe to test upipe_genaux */
static struct upipe *genaux_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe, struct ulog *ulog)
{
    struct genaux_test *genaux_test = malloc(sizeof(struct genaux_test));
    assert(genaux_test != NULL);
    upipe_init(&genaux_test->upipe, mgr, uprobe, ulog);
    genaux_test->flow = NULL;
    genaux_test->entry = NULL;
    return &genaux_test->upipe;
}

/** helper phony pipe to test upipe_genaux */
static void genaux_test_input(struct upipe *upipe, struct uref *uref,
                              struct upump *upump)
{
    struct genaux_test *genaux_test = genaux_test_from_upipe(upipe);
    const char *def;
    assert(uref != NULL);
    ulog_debug(upipe->ulog, "===> received input uref");
    udict_dump(uref->udict, upipe->ulog);

    if (unlikely(uref_flow_get_def(uref, &def))) {
        assert(def);
        if (genaux_test->flow) {
            uref_free(genaux_test->flow);
        }
        genaux_test->flow = uref;
        ulog_debug(upipe->ulog, "flow def %s", def);
        return;
    }
    if (genaux_test->entry) {
        uref_free(genaux_test->entry);
    }
    genaux_test->entry = uref;
    // FIXME peek into buffer
}

/** helper phony pipe to test upipe_genaux */
static void genaux_test_release(struct upipe *upipe)
{
    ulog_debug(upipe->ulog, "releasing pipe %p", upipe);
    struct genaux_test *genaux_test = genaux_test_from_upipe(upipe);
    if (genaux_test->entry)
        uref_free(genaux_test->entry);
    if (genaux_test->flow)
        uref_free(genaux_test->flow);
    upipe_clean(upipe);
    free(genaux_test);
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr genaux_test_mgr = {
    .upipe_alloc = genaux_test_alloc,
    .upipe_input = genaux_test_input,
    .upipe_control = NULL,
    .upipe_use = NULL,
    .upipe_release = genaux_test_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};



int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    struct ubuf_mgr *ubuf_mgr;
    struct uref *uref;
    uint64_t systime, result;
    uint8_t buf[8];

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    struct ulog *mainlog = ulog_stdio_alloc(stdout, ULOG_LEVEL, "main");

    /* block */
    ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                    UBUF_POOL_DEPTH, umem_mgr,
                                                    UBUF_PREPEND, UBUF_APPEND,
                                                    UBUF_ALIGN,
                                                    UBUF_ALIGN_OFFSET);
    assert(ubuf_mgr);


    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_print = uprobe_print_alloc(&uprobe, stdout, "test");
    assert(uprobe_print != NULL);

    /* clock */
    struct uclock *uclock = uclock_std_alloc(0);
    assert(uclock != NULL);
    systime = uclock_now(uclock);
    uclock_release(uclock);

    /* build genaux pipe */
    struct upipe_mgr *upipe_genaux_mgr = upipe_genaux_mgr_alloc();
    struct upipe *genaux = upipe_alloc(upipe_genaux_mgr, uprobe_print, ulog_stdio_alloc(stdout, ULOG_LEVEL, "genaux"));
    assert(upipe_genaux_mgr);
    assert(genaux);
    assert(upipe_set_ubuf_mgr(genaux, ubuf_mgr));

    struct upipe *genaux_test = upipe_alloc(&genaux_test_mgr, uprobe_print, ulog_stdio_alloc(stdout, ULOG_LEVEL, "genaux_test"));
    assert(upipe_set_output(genaux, genaux_test));

    /* Send first flow definition packet */
    uref = uref_block_flow_alloc_def(uref_mgr, "bar.");
    assert(uref);
    upipe_input(genaux, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr, 42);
    assert(uref);
    assert(uref_clock_set_systime(uref, systime));
    /* Now send uref */
    upipe_input(genaux, uref, NULL);

    uref_block_extract(genaux_test_from_upipe(genaux_test)->entry, 0, sizeof(uint64_t), buf);
    result = upipe_genaux_ntoh64(buf);
    ulog_debug(mainlog, "original: %"PRIu64" \t result: %"PRIu64, systime, result);
    assert(systime == result);

    upipe_release(genaux);

    /* release managers */
    ubuf_mgr_release(ubuf_mgr);
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_print_free(uprobe_print);
    ulog_free(mainlog);

    return 0;
}

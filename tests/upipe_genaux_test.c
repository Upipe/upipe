/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_clock.h>
#include <upipe-modules/upipe_genaux.h>

#include <upipe/upipe_helper_upipe.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    0
#define UREF_POOL_DEPTH     0
#define UBUF_POOL_DEPTH     0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
    }
    return UBASE_ERR_NONE;
}

/** helper phony pipe */
struct genaux_test {
    struct uref *entry;
    struct upipe upipe;
};

/** helper phony pipe */
UPIPE_HELPER_UPIPE(genaux_test, upipe, 0);

/** helper phony pipe */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe,
                                uint32_t signature, va_list args)
{
    struct genaux_test *genaux_test = malloc(sizeof(struct genaux_test));
    assert(genaux_test != NULL);
    upipe_init(&genaux_test->upipe, mgr, uprobe);
    genaux_test->entry = NULL;
    return &genaux_test->upipe;
}

/** helper phony pipe */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump **upump_p)
{
    struct genaux_test *genaux_test = genaux_test_from_upipe(upipe);
    assert(uref != NULL);
    upipe_dbg(upipe, "===> received input uref");
    uref_dump(uref, upipe->uprobe);

    if (genaux_test->entry) {
        uref_free(genaux_test->entry);
    }
    genaux_test->entry = uref;
    // FIXME peek into buffer
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *urequest = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, urequest);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe */
static void test_free(struct upipe *upipe)
{
    upipe_dbg_va(upipe, "releasing pipe %p", upipe);
    struct genaux_test *genaux_test = genaux_test_from_upipe(upipe);
    if (genaux_test->entry)
        uref_free(genaux_test->entry);
    upipe_clean(upipe);
    free(genaux_test);
}

/** helper phony pipe */
static struct upipe_mgr genaux_test_mgr = {
    .refcount = NULL,
    .signature = 0,

    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);

    struct uref *uref;
    uint64_t opaque = 0xcafebabedeadbeef, result;
    uint8_t buf[8];

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(uref_mgr != NULL);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_DEBUG);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* set up flow definition packet */
    uref = uref_block_flow_alloc_def(uref_mgr, "bar.");
    assert(uref);

    /* build genaux pipe */
    struct upipe_mgr *upipe_genaux_mgr = upipe_genaux_mgr_alloc();
    struct upipe *genaux = upipe_void_alloc(upipe_genaux_mgr,
            uprobe_pfx_alloc(uprobe_use(logger), UPROBE_LOG_LEVEL, "genaux"));
    assert(upipe_genaux_mgr);
    ubase_assert(upipe_set_flow_def(genaux, uref));
    assert(genaux);

    uref_free(uref);
    ubase_assert(upipe_get_flow_def(genaux, &uref));
    const char *def;
    ubase_assert(uref_flow_get_def(uref, &def));
    assert(!strcmp(def, "block.aux."));

    struct upipe *genaux_test = upipe_void_alloc(&genaux_test_mgr,
                                                 uprobe_use(logger));
    assert(genaux_test != NULL);
    ubase_assert(upipe_set_output(genaux, genaux_test));

    uref = uref_alloc(uref_mgr);
    assert(uref);
    uref_clock_set_cr_sys(uref, opaque);
    /* Now send uref */
    upipe_input(genaux, uref, NULL);

    assert(genaux_test_from_upipe(genaux_test)->entry);
    uref_block_extract(genaux_test_from_upipe(genaux_test)->entry, 0, sizeof(uint64_t), buf);
    result = upipe_genaux_ntoh64(buf);
    uprobe_dbg_va(logger, NULL, "original: %"PRIu64" \t result: %"PRIu64, opaque, result);
    assert(opaque == result);

    /* test arbitrary geattr */
    ubase_assert(upipe_genaux_set_getattr(genaux, uref_clock_get_pts_prog));
    uref = uref_alloc(uref_mgr);
    assert(uref);
    uref_clock_set_pts_prog(uref, opaque);
    upipe_input(genaux, uref, NULL);

    uref_block_extract(genaux_test_from_upipe(genaux_test)->entry, 0, sizeof(uint64_t), buf);
    result = upipe_genaux_ntoh64(buf);
    uprobe_dbg_va(logger, NULL, "original: %"PRIu64" \t result: %"PRIu64, opaque, result);
    assert(opaque == result);

    upipe_release(genaux);
    test_free(genaux_test);

    /* release managers */
    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(logger);
    uprobe_clean(&uprobe);

    return 0;
}

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
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe-modules/upipe_skip.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#define UDICT_POOL_DEPTH    5
#define UREF_POOL_DEPTH     5
#define UBUF_POOL_DEPTH     5
#define UBUF_PREPEND        0
#define UBUF_APPEND         0
#define UBUF_ALIGN          32
#define UBUF_ALIGN_OFFSET   0

#define ITERATIONS  50
#define SIZE        1024
#define TESTSTR     "CAFEBABEDEADBEEF"
#define TESTSTRSUB  "DEADBEEF"
#define OFFSET      8
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

/** phony pipe to test upipe_skip */
struct skip_test {
    int counter;
    struct upipe upipe;
};

/** helper phony pipe to test upipe_skip */
UPIPE_HELPER_UPIPE(skip_test, upipe, 0);

/** helper phony pipe to test upipe_skip */
static struct upipe *test_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct skip_test *skip_test = malloc(sizeof(struct skip_test));
    assert(skip_test != NULL);
    upipe_init(&skip_test->upipe, mgr, uprobe);
    skip_test->counter = 0;
    upipe_throw_ready(&skip_test->upipe);
    return &skip_test->upipe;
}

/** helper phony pipe to test upipe_skip */
static void test_input(struct upipe *upipe, struct uref *uref,
                            struct upump **upump_p)
{
    struct skip_test *skip_test = skip_test_from_upipe(upipe);
    const uint8_t *buf;
    int size;

    size = -1;
    ubase_assert(uref_block_read(uref, 0, &size, &buf));
    assert(!memcmp(buf, TESTSTRSUB, sizeof(TESTSTRSUB)));
    printf("%d \"%s\"\n", skip_test->counter, buf);
    uref_block_unmap(uref, 0);

    skip_test->counter++;

    uref_free(uref);
}

/** helper phony pipe */
static int test_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return UBASE_ERR_NONE;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe to test upipe_skip */
static void test_free(struct upipe *upipe)
{
    struct skip_test *skip_test = skip_test_from_upipe(upipe);
    assert(skip_test->counter == ITERATIONS);
    upipe_throw_dead(upipe);
    upipe_clean(upipe);
    free(skip_test);
}

/** helper phony pipe to test upipe_skip */
static struct upipe_mgr skip_test_mgr = {
    .refcount = NULL,
    .signature = 0,
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = test_control
};


/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe, int event, va_list args)
{
    switch (event) {
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEW_FLOW_DEF:
            break;
        default:
            assert(0);
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    printf("Compiled %s %s - %s\n", __DATE__, __TIME__, __FILE__);
    int i;
    struct uref *uref = NULL;
    uint8_t *buf;
    int size;

    /* uref and mem management */
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0); 
    assert(uref_mgr != NULL);

    /* block */
    struct ubuf_mgr *block_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
            UBUF_POOL_DEPTH, umem_mgr,
            UBUF_ALIGN,
            UBUF_ALIGN_OFFSET);
    assert(block_mgr);

    /* uprobe stuff */
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_DEBUG);
    assert(uprobe_stdio != NULL);

    uref = uref_block_flow_alloc_def(uref_mgr, "foo.");
    assert(uref);

    /* build skip pipe */
    struct upipe_mgr *upipe_skip_mgr = upipe_skip_mgr_alloc();
    assert(upipe_skip_mgr);
    struct upipe *skip = upipe_void_alloc(upipe_skip_mgr,
                uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                                 "skip"));
    assert(skip);
    ubase_assert(upipe_set_flow_def(skip, uref));

    uref_free(uref);
    ubase_assert(upipe_get_flow_def(skip, &uref));
    const char *def;
    ubase_assert(uref_flow_get_def(uref, &def));
    assert(!strcmp(def, "block.foo."));

    /* skip_test */
    struct upipe *skip_test = upipe_void_alloc(&skip_test_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "skiptest"));
    assert(skip_test != NULL);
    ubase_assert(upipe_set_output(skip, skip_test));
    upipe_release(skip_test);
    ubase_assert(upipe_skip_set_offset(skip, OFFSET));

    /* Now send uref */
    for (i=0; i < ITERATIONS; i++) {
        uref = uref_block_alloc(uref_mgr, block_mgr, SIZE);
        size = -1;
        uref_block_write(uref, 0, &size, &buf);
        memcpy(buf, TESTSTR, sizeof(TESTSTR));
        uref_block_unmap(uref, 0);
        upipe_input(skip, uref, NULL);
    }

    /* release pipe */
    upipe_release(skip);
    test_free(skip_test);

    /* release managers */
    upipe_mgr_release(upipe_skip_mgr); // no-op
    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(block_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}

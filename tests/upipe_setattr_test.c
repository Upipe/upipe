/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short unit tests for setattr pipe
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_log.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-modules/upipe_setattr.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 10
#define UREF_POOL_DEPTH 10
#define UBUF_POOL_DEPTH 10
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

UREF_ATTR_STRING(test, 1, "x.test1", test 1)
UREF_ATTR_UNSIGNED(test, 2, "x.test2", test 2)

static unsigned int nb_packets = 0;

/** definition of our uprobe */
static bool catch(struct uprobe *uprobe, struct upipe *upipe,
                  enum uprobe_event event, va_list args)
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

/** helper phony pipe to test upipe_setattr */
static struct upipe *test_alloc(struct upipe_mgr *mgr, struct uprobe *uprobe)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    return upipe;
}

/** helper phony pipe to test upipe_setattr */
static void test_input(struct upipe *upipe, struct uref *uref,
                       struct upump *upump)
{
    assert(uref != NULL);
    const char *def;
    if (uref_flow_get_def(uref, &def)) {
        uref_free(uref);
        return;
    }

    const char *string;
    assert(uref_test_get_1(uref, &string));
    assert(!strcmp(string, "test"));
    uint64_t num;
    assert(uref_test_get_2(uref, &num));
    assert(num == 42);
    uref_free(uref);
    nb_packets++;
}

/** helper phony pipe to test upipe_setattr */
static void test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test upipe_setattr */
static struct upipe_mgr test_mgr = {
    .upipe_alloc = test_alloc,
    .upipe_input = test_input,
    .upipe_control = NULL,
    .upipe_free = NULL,

    .upipe_mgr_free = NULL
};

int main(int argc, char *argv[])
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    struct uprobe *log = uprobe_log_alloc(uprobe_stdio, UPROBE_LOG_LEVEL);
    assert(log != NULL);

    struct upipe *upipe_sink = upipe_alloc(&test_mgr, log);
    assert(upipe_sink != NULL);

    struct upipe_mgr *upipe_setattr_mgr = upipe_setattr_mgr_alloc();
    assert(upipe_setattr_mgr != NULL);
    struct upipe *upipe_setattr = upipe_alloc(upipe_setattr_mgr,
            uprobe_pfx_adhoc_alloc(log, UPROBE_LOG_LEVEL, "setattr"));
    assert(upipe_setattr != NULL);
    assert(upipe_set_output(upipe_setattr, upipe_sink));

    struct uref *uref;
    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    assert(uref_flow_set_def(uref, "internal."));
    upipe_input(upipe_setattr, uref, NULL);

    struct uref *dict = uref_alloc(uref_mgr);
    assert(uref_test_set_1(dict, "test"));
    assert(uref_test_set_2(dict, 42));
    assert(upipe_setattr_set_dict(upipe_setattr, dict));
    uref_free(dict);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_setattr, uref, NULL);

    uref = uref_alloc(uref_mgr);
    assert(uref != NULL);
    upipe_input(upipe_setattr, uref, NULL);

    assert(nb_packets == 2);

    upipe_release(upipe_setattr);
    upipe_mgr_release(upipe_setattr_mgr); // nop

    test_free(upipe_sink);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_log_free(log);
    uprobe_stdio_free(uprobe_stdio);

    return 0;
}

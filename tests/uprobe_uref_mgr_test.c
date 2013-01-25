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
 * @short unit tests for uprobe_uref_mgr implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

static struct uref_mgr *uref_mgr;
static bool got_uref_mgr = false;

/** helper phony pipe to test uprobe_uref_mgr */
static struct upipe *uprobe_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       struct ulog *ulog)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe, ulog);
    upipe_throw_need_uref_mgr(upipe);
    assert(got_uref_mgr);
    return upipe;
}

/** helper phony pipe to test uprobe_uref_mgr */
static bool uprobe_test_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *mgr = va_arg(args, struct uref_mgr *);
            assert(mgr == uref_mgr);
            got_uref_mgr = true;
            return true;
        }

        default:
            assert(0);
            return false;
    }
}

/** helper phony pipe to test uprobe_uref_mgr */
static void uprobe_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_uref_mgr */
static struct upipe_mgr uprobe_test_mgr = {
    .upipe_alloc = uprobe_test_alloc,
    .upipe_input = NULL,
    .upipe_control = uprobe_test_control,
    .upipe_use = NULL,
    .upipe_release = NULL,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

int main(int argc, char **argv)
{
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);

    struct uprobe *uprobe = uprobe_uref_mgr_alloc(NULL, uref_mgr);
    assert(uprobe != NULL);

    struct upipe *upipe = uprobe_test_alloc(&uprobe_test_mgr, uprobe,
            ulog_stdio_alloc(stdout, ULOG_DEBUG, "probe"));
    uprobe_test_free(upipe);

    uprobe_uref_mgr_free(uprobe);

    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}

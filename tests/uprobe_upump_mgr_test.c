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
 * @short unit tests for uprobe_upump_mgr implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/upipe.h>
#include <upipe/upump.h>
#include <upump-ev/upump_ev.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1

static struct upump_mgr *upump_mgr;
static bool got_upump_mgr = false;

/** helper phony pipe to test uprobe_upump_mgr */
static struct upipe *uprobe_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    upipe_throw_need_upump_mgr(upipe);
    assert(got_upump_mgr);
    return upipe;
}

/** helper phony pipe to test uprobe_upump_mgr */
static bool uprobe_test_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *mgr = va_arg(args, struct upump_mgr *);
            assert(mgr == upump_mgr);
            got_upump_mgr = true;
            return true;
        }

        default:
            assert(0);
            return false;
    }
}

/** helper phony pipe to test uprobe_upump_mgr */
static void uprobe_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_upump_mgr */
static struct upipe_mgr uprobe_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = uprobe_test_alloc,
    .upipe_input = NULL,
    .upipe_control = uprobe_test_control
};

int main(int argc, char **argv)
{
    struct ev_loop *loop = ev_default_loop(0);
    upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL, UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct uprobe *uprobe = uprobe_upump_mgr_alloc(NULL, upump_mgr);
    assert(uprobe != NULL);
    upump_mgr_release(upump_mgr);

    struct upipe *upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe);
    uprobe_test_free(upipe);

    ev_default_destroy();
    return 0;
}

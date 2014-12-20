/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
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
 * @short unit tests for uprobe_uclock implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_uclock.h>
#include <upipe/upipe.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>
#include <upipe/urequest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static struct uclock *uclock;

/** helper phony pipe to test uprobe_ubuf_mem */
static int uprobe_test_provide_uclock(struct urequest *urequest, va_list args)
{
    struct uclock *m = va_arg(args, struct uclock *);
    assert(m == uclock);
    uclock_release(m);
    return UBASE_ERR_NONE;
}

/** helper phony pipe to test uprobe_uclock */
static struct upipe *uprobe_test_alloc(struct upipe_mgr *mgr,
                                       struct uprobe *uprobe,
                                       uint32_t signature, va_list args)
{
    struct upipe *upipe = malloc(sizeof(struct upipe));
    assert(upipe != NULL);
    upipe_init(upipe, mgr, uprobe);
    struct urequest request;
    urequest_init_uclock(&request, uprobe_test_provide_uclock, NULL);
    return upipe;
}

/** helper phony pipe to test uprobe_uclock */
static void uprobe_test_free(struct upipe *upipe)
{
    upipe_clean(upipe);
    free(upipe);
}

/** helper phony pipe to test uprobe_uclock */
static struct upipe_mgr uprobe_test_mgr = {
    .refcount = NULL,
    .upipe_alloc = uprobe_test_alloc,
    .upipe_input = NULL,
    .upipe_control = NULL
};

int main(int argc, char **argv)
{
    uclock = uclock_std_alloc(0);
    assert(uclock != NULL);

    struct uprobe *uprobe = uprobe_uclock_alloc(NULL, uclock);
    assert(uprobe != NULL);

    struct upipe *upipe = upipe_void_alloc(&uprobe_test_mgr, uprobe);
    uprobe_test_free(upipe);

    uclock_release(uclock);
    return 0;
}

/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe-modules/upipe_blit.h>
#include <upipe-modules/uprobe_blit_prepare.h>

struct uprobe_blit_prepare {
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_blit_prepare, uprobe);

static int uprobe_blit_prepare_throw(struct uprobe *uprobe,
                                     struct upipe *upipe,
                                     int event, va_list args)
{
    if (event != UPROBE_BLIT_PREPARE_READY ||
        ubase_get_signature(args) != UPIPE_BLIT_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_BLIT_SIGNATURE);
    struct upump **upump_p = va_arg(args, struct upump **);
    return upipe_blit_prepare(upipe, upump_p);
}

static struct uprobe *uprobe_blit_prepare_init(
    struct uprobe_blit_prepare *uprobe_blit_prepare,
    struct uprobe *next)
{
    assert(uprobe_blit_prepare);
    struct uprobe *uprobe = uprobe_blit_prepare_to_uprobe(uprobe_blit_prepare);
    uprobe_init(uprobe, uprobe_blit_prepare_throw, next);
    return uprobe;
}

static void uprobe_blit_prepare_clean(
    struct uprobe_blit_prepare *uprobe_blit_prepare)
{
    assert(uprobe_blit_prepare);
    uprobe_clean(uprobe_blit_prepare_to_uprobe(uprobe_blit_prepare));
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_blit_prepare)
#undef ARGS
#undef ARGS_DECL

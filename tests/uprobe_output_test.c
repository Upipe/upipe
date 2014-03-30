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
 * @short unit tests for uprobe output implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_output.h>
#include <upipe/upipe.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_std.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0

static struct upipe *output = NULL;
static enum ubase_err set_flow_def_answer = UBASE_ERR_NONE;
static bool expect_new_flow_def = true;

/** definition of our uprobe */
static enum ubase_err catch(struct uprobe *uprobe, struct upipe *upipe,
                            int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_NEW_FLOW_DEF:
            assert(expect_new_flow_def);
            expect_new_flow_def = false;
            break;
    }
    return UBASE_ERR_NONE;
}

static enum ubase_err test_control(struct upipe *upipe,
                                   int command, va_list args)
{
    switch (command) {
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            assert(p != NULL);
            *p = output;
            return UBASE_ERR_NONE;
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *s = va_arg(args, struct upipe *);
            output = s;
            return UBASE_ERR_NONE;
        }
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr test_mgr = {
    .refcount = NULL,
    .signature = 0,

    .upipe_alloc = NULL,
    .upipe_input = NULL,
    .upipe_control = test_control
};

static enum ubase_err output_control(struct upipe *upipe,
                                     int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF:
            return set_flow_def_answer;
        default:
            assert(0);
            return UBASE_ERR_UNHANDLED;
    }
}

/** helper phony pipe to test upipe_dup */
static struct upipe_mgr output_mgr = {
    .refcount = NULL,
    .signature = 0,

    .upipe_alloc = NULL,
    .upipe_input = NULL,
    .upipe_control = output_control
};

int main(int argc, char **argv)
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
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_DEBUG);
    assert(logger != NULL);

    struct uprobe *uprobe_output = uprobe_output_alloc(logger);
    assert(uprobe_output != NULL);

    struct upipe test_pipe;
    test_pipe.refcount = NULL;
    test_pipe.uprobe = uprobe_output;
    test_pipe.mgr = &test_mgr;
    struct upipe *upipe = &test_pipe;

    struct upipe output_pipe;
    output_pipe.refcount = NULL;
    output_pipe.uprobe = NULL;
    output_pipe.mgr = &output_mgr;

    struct uref *uref = uref_alloc_control(uref_mgr);
    assert(uref != NULL);
    ubase_assert(uref_flow_set_def(uref, "void."));

    upipe_throw_new_flow_def(upipe, uref);
    assert(!expect_new_flow_def);

    ubase_assert(upipe_set_output(upipe, &output_pipe));
    upipe_throw_new_flow_def(upipe, uref);
    assert(output == &output_pipe);

    set_flow_def_answer = UBASE_ERR_INVALID;
    expect_new_flow_def = true;
    upipe_throw_new_flow_def(upipe, uref);
    assert(!expect_new_flow_def);
    assert(output == NULL);

    uref_free(uref);
    uprobe_release(uprobe_output);

    uref_mgr_release(uref_mgr);
    umem_mgr_release(umem_mgr);
    udict_mgr_release(udict_mgr);
    return 0;
}

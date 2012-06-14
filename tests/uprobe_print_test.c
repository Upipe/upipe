/*****************************************************************************
 * uprobe_print_test.c: unit tests for uprobe print implementation
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_print.h>
#include <upipe/upipe.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

static struct upipe test_pipe;

int main(int argc, char **argv)
{
    printf("%p\n", &test_pipe);

    struct uprobe *uprobe = uprobe_print_alloc(stdout, "test");
    assert(uprobe != NULL);
    test_pipe.uprobe = uprobe;

    upipe_throw_aerror(&test_pipe);
    upipe_throw_upump_error(&test_pipe);
    upipe_throw_read_end(&test_pipe, "pouet");
    upipe_throw_write_end(&test_pipe, "pouet");

    uprobe_print_free(uprobe);
    uprobe = uprobe_print_alloc_va(stdout, "test %d", 2);
    assert(uprobe != NULL);
    test_pipe.uprobe = uprobe;

    upipe_throw_new_flow(&test_pipe, "output", NULL);
    upipe_throw_need_uref_mgr(&test_pipe);
    upipe_throw_need_upump_mgr(&test_pipe);
    uprobe_print_free(uprobe);
    return 0;
}

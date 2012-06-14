/*****************************************************************************
 * ulog_std_test.c: unit tests for ulog implementation
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

#include <upipe/ulog.h>
#include <upipe/ulog_std.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct ulog *ulog1 = ulog_std_alloc(stdout, ULOG_DEBUG, "test");
    assert(ulog1 != NULL);

    ulog_error(ulog1, "This is an error");
    ulog_warning(ulog1, "This is a %s warning with %d", "composite", 0x42);
    ulog_notice(ulog1, "This is a notice");
    ulog_debug(ulog1, "This is a debug, next error is an allocation failure");
    ulog_aerror(ulog1);
    ulog_free(ulog1);

    struct ulog *ulog2 = ulog_std_alloc_va(stdout, ULOG_ERROR, "test[%d]", 2);
    assert(ulog2 != NULL);
    ulog_error(ulog2, "This is another error with %d", 0x43);
    ulog_warning(ulog2, "This is a warning that you shouldn't see");
    ulog_free(ulog2);
    return 0;
}
